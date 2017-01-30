#include <windows.h>
#include "mysql.h"

#include <algorithm>
#include <iostream>
#include <cstdio>
#include <fstream>
#include "drpdb.h"
#include "CSVWriter.h"
#include "SQLSchemaWriter.h"

#undef max
const auto pdbidTableMarker = std::numeric_limits<size_t>::max();

namespace
{
	struct SingleConnection
	{
		MYSQL* mysql;
		SingleConnection(const char* host, const char* user, const char* pass, const char* db, int port)
		{
			mysql = mysql_init(nullptr);
			if (mysql)
			{
				my_bool auto_reconnect = true;
				mysql_options(mysql, MYSQL_OPT_RECONNECT, &auto_reconnect);
				if (!mysql_real_connect(mysql, host, user, pass, db, port, nullptr, 0))
				{
					set_error(mysql_error(mysql));
				}
			}
			else
			{
				set_error("Failed to initialize mysql");
			}
		}
		void execute(const char* begin, const char* end)
		{
			if (mysql)
			{
				if (mysql_real_query(mysql, begin, static_cast<unsigned long>(end - begin)) == 0)
				{
					if (mysql_warning_count(mysql) > 0)
					{
						int a = 2;
						++a;
					}
				}
				else
				{
					set_error(mysql_error(mysql));
				}
			}
		}
		~SingleConnection()
		{
			if (mysql)
				mysql_close(mysql);
		}
	};
	struct OutputData
	{
		const SymbolData& Results;
		std::vector<std::string> UploadCommands;
		std::string tempdir_escaped;
		std::string tempdir;
		int port;
		std::string host;
		std::string user;
		std::string pass;
		std::string db;
		OutputData(const SymbolData& Res)
			:Results(Res)
		{
		}
		void init(size_t pdbId)
		{
			host = getOption("-host");
			user = getOption("-user");
			pass = getOption("-pass");
			db = getOption("-db");


			if (host.empty() || user.empty() || db.empty())
			{
				throw "MySQL requires -host, -user, and -db settings.";
			}
			auto portstr = getOption("-port");
			if (portstr.empty())
			{
				port = 3306;
			}
			else
			{
				port = atoi(portstr.data());
				if (port == 0)
				{
					throw "Invalid -port specified.";
				}
			}

			tempdir = getOption("-tempdir");
			if (tempdir.size() == 0)
			{
				char buf[MAX_PATH + 1] = {};
				auto len = GetCurrentDirectoryA(MAX_PATH, buf);
				if (len > MAX_PATH)
					throw "tempdir path too long";
				tempdir = buf;
				tempdir += "\\temp\\";
			}
			CreateDirectoryA(tempdir.data(), nullptr);
			tempdir_escaped = replace(tempdir, "\\", "\\\\") + "\\\\";

			GenerateCommands(pdbId);
		}
		void AddPdbIdsTable(const std::vector<std::string>& pdbs)
		{
			std::vector<PdbIdTable> pdbTable;
			pdbTable.reserve(pdbs.size());
			for (const auto& pdb : pdbs)
			{
				pdbTable.emplace_back(PdbIdTable{pdb});
			}
			BuildTable(pdbTable, CSV::details::pdbColumn(), "Assigment of PDB files to their IDs", pdbidTableMarker);
		}
		void LoadTable(const std::string& name, const std::string& end)
		{
			std::string Cmd = "load data local infile '";
			Cmd += tempdir_escaped;
			Cmd += name;
			Cmd += "_values.txt' into table ";
			Cmd += name;
			Cmd += " fields terminated by ',' optionally enclosed by '\"' lines terminated by '\\n'";
			Cmd += end;
			Cmd += ";";

			UploadCommands.push_back(Cmd);
		}
		template<class T>
		void CreateTable(std::string tablename, std::string& EndClause, const char* desc, size_t pdbid)
		{
			const bool pdbtable = pdbid == pdbidTableMarker;
			const bool first = (pdbid == 0) || pdbtable;

			T Value;
			SQL::schema_writer Ar(true);
			if (first)
			{
				UploadCommands.push_back(std::string("DROP TABLE IF EXISTS ") + tablename + ";");

				Ar.Result += "CREATE TABLE ";
				Ar.Result += tablename;
				Ar.Result += "(";
			}
			Ar << Value;
			if (first)
			{
				Ar.Result += Ar.Keys;
				if (!pdbtable)
				{
					Ar.Result += CSV::details::pdbColumn();
					Ar.Result += " INT UNSIGNED NOT NULL";
					Ar.Result += " COMMENT 'id of the PDB'";
				}
				Ar.Result += ") Engine=MyISAM";
				Ar.Result += " COMMENT='";
				std::string temp_desc = desc;
				temp_desc = replace(temp_desc, "'", "''");
				Ar.Result += temp_desc;
				Ar.Result += "';";
				UploadCommands.push_back(Ar.Result);
			}

			if (!pdbtable)
			{
				Ar.LoadClause += CSV::details::pdbColumn();
			}
			EndClause = Ar.LoadClause + ") " + Ar.SetClause;
		}


		template<class T>
		void PopulateTable(T TableBegin, T TableEnd, const std::string& name, size_t pdbId)
		{
			const bool pdbTablePopulated = pdbId == pdbidTableMarker;
			pdbId = pdbTablePopulated ? 0 : pdbId;
			CSV::writer Ar(tempdir + name + "_values.txt", true, ',');
			while (TableBegin != TableEnd)
			{
				if (pdbTablePopulated)
				{
					Ar.out += std::to_string(pdbId++);
					Ar.out += ',';
					Ar << *TableBegin;
				}
				else
				{
					Ar << *TableBegin;
					Ar.out += std::to_string(pdbId);
				}
				Ar.out += "\n";
				++TableBegin;
			}
			std::ofstream writer(Ar.outPath, std::ios::out | std::ios::binary);
			writer << Ar.out;
		}

		template<class T>
		void BuildTable(const std::vector<T>& Table, const std::string& name, const char* desc, size_t pdbid)
		{
			std::string EndClause;
			CreateTable<T>(name, EndClause, desc, pdbid);
			PopulateTable(Table.begin(), Table.end(), name, pdbid);
			LoadTable(name, EndClause);
		}

		template<class U, class T>
		void BuildTable(const std::unordered_map<U, T>& Table, const std::string& name, const char* desc, size_t pdbid)
		{
			std::string EndClause;
			CreateTable<T>(name, EndClause, desc, pdbid);

			if (Results.Populate)
			{
				PopulateTable(Table.begin(), Table.end(), name, pdbid);
			}
			LoadTable(name, EndClause);

		}

		void ParseProcedure(const std::string& sequence)
		{
			size_t prev = 0;
			auto next = sequence.find("#then_execute");
			while (next != std::string::npos)
			{
				UploadCommands.emplace_back(sequence.substr(prev, next));
				prev = next + 14;
				next = sequence.find("#then_execute", prev);
			}
			UploadCommands.emplace_back(sequence.substr(prev));
		}
		void GenerateProcedures()
		{
			WIN32_FIND_DATAA found;
			auto search = FindFirstFileA("../../config/mysql/*.sql", &found);
			while (search != INVALID_HANDLE_VALUE)
			{
				std::ifstream file(std::string("../../config/mysql/") + found.cFileName);
				if (file.good())
				{
					std::string file_contents{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
					ParseProcedure(file_contents);
				}
				if (!FindNextFileA(search, &found))
				{
					break;
				}
			}
		}
		void GenerateCommands(size_t pdbid)
		{
			if (pdbid == 0)
			{
				GenerateProcedures();
			}
#define BEGIN_STRUCT(type, name, desc,category) BuildTable(Results.type, #name, desc, pdbid );

#include "PDBReflection.inl"
		}
	};
}
namespace MySQL
{
	namespace
	{
		void output(const SymbolData& Data, size_t pdbId, const std::vector<std::string>& pdbs)
		{
			OutputData Result(Data);
			Result.init(pdbId);
			if (pdbId == 0)
			{
				Result.AddPdbIdsTable(pdbs);
			}

			SingleConnection conn(Result.host.data(), Result.user.data(), Result.pass.data(), Result.db.data(), Result.port);

			int i = 0;
			while (!has_error() && i<Result.UploadCommands.size())
			{
				conn.execute(Result.UploadCommands[i].data(), Result.UploadCommands[i].data() + Result.UploadCommands[i].size());
				++i;
			}
		}

		static std::string describe()
		{
			return
				"    req: -host=<database server>\n    req: -user=<username>\n    req: -db=<database>\n    opt: -pass=<password>\n    opt: -port=<port number> (default 3306)\n    opt: -temp=<custom temp directory>";
		}
	}

	OutputEngine CreateEngine()
	{
		OutputEngine res;
		res.name = "mysql";
		res.output = &output;
		res.describe = &describe;
		return res;
	}
}
