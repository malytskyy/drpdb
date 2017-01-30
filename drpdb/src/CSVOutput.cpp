#include <algorithm>
#include <iostream>
#include <cstdio>
#include <windows.h>
#include <fstream>
#include "drpdb.h"
#include "CSVWriter.h"
#include <locale>
#include <codecvt>



namespace
{
	struct Output
	{
		const SymbolData& Results;
		std::string outdir;
		bool naked;
		char separator = ',';
		Output(const SymbolData& Res)
			: Results(Res)
			, separator()
		{
		}
		void init()
		{
			outdir = getOption("-outdir");
			if (outdir.empty())
			{
				char buf[MAX_PATH] = {};
				GetCurrentDirectoryA(MAX_PATH, buf);
				outdir = buf;
			}
			naked = getFlag("-nocolumnheaders");
			separator = CSV::details::getSeparator();
		}

		void AppendHeader(std::string table, std::string& out)
		{
#define BEGIN_STRUCT(type, name, desc, category) if (table == #name ) {
#define MEMBER(name, desc)  out += ( #name );  out += separator;
#define END_STRUCT() ; out += CSV::details::pdbColumn(); out+="\n"; }

#include "PDBReflection.inl"

		}


		template<class T>
		void PopulateTable(T TableBegin, T TableEnd, const std::string& name, size_t pdbId)
		{
			CSV::writer Ar(outdir + name + ".csv", true, separator);
			const bool first = pdbId == 0;
			if (first && !naked)
			{
				AppendHeader(name, Ar.out);
			}
			while (TableBegin != TableEnd)
			{
				Ar << *TableBegin;
				Ar.out += std::to_string(pdbId);
				Ar.out += "\n";
				++TableBegin;
			}
			const auto openMode = !first ?
				std::ios::in | std::ios::out | std::ios::binary | std::ios::ate :
				std::ios::out | std::ios::binary;
			std::ofstream writer(Ar.outPath, openMode);
			writer << Ar.out;
			if (writer.bad())
			{
				std::string err = "Write to ";
				err += Ar.outPath;
				err += " failed";
				set_error(err.data());
			}
		}

		template<class T>
		void BuildTable(const T& Table, const std::string& name, size_t pdbId)
		{
			PopulateTable(Table.begin(), Table.end(), name, pdbId);
		}

		void GenerateCommands(size_t pdbId)
		{
#define BEGIN_STRUCT(type, name, desc, category) BuildTable(Results.type, #name, pdbId );

#include "PDBReflection.inl"

		}
		void AddPdbIdsTable(const std::vector<std::string>& pdbs)
		{
			const auto openMode = std::ios::out | std::ios::binary ;
			auto path = outdir + CSV::details::pdbColumn() + ".csv";
			std::ofstream writer(path, openMode);
			if (!naked)
			{
				writer	<< CSV::details::pdbColumn() << separator
					<< CSV::details::pdbNameColumn()
					<< "\n";
			}
			size_t pdbId = 0;
			for (const auto& pdb : pdbs)
			{
				writer	<< ++pdbId
					<< CSV::details::getSeparator()
					<< pdb
					<< "\n";
			}
			if (writer.bad())
			{
				std::string err = "Write to ";
				err += path;
				err += " failed";
				set_error(err.data());
			}
		}
	};
}
namespace CSV
{
	void writer::operator<<(float V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%f", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(int V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%d", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(uint32_t V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%u", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(unsigned long long V)
	{
		char Buf[90];
		_snprintf_s(Buf, 64, "%llu", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(long long V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%lld", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(long V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%ld", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(unsigned long V)
	{
		char Buf[64];
		_snprintf_s(Buf, 64, "%lu", V);
		out += Buf;
		out += separator;
	}
	void writer::operator<<(const Sym::address_info& V)
	{
		*this << V.rv;
	}
	void writer::operator<<(const std::string& V)
	{
		out += "\"";
		auto copy = V;
		escape(copy, separator);
		out += std::move(copy);
		out += "\"";
		out += separator;
	}
	void writer::operator<<(bool V)
	{
		if (UseBitType)
		{
			out += V ? '1' : '0';
		}
		else
		{
			out += V ? "TRUE" : "FALSE";
		}
		out += separator;
	}
	void writer::operator<<(const PdbIdTable V)
	{
		out += "\"";
		auto copy = V.PdbFile;
		escape(copy, separator);
		out += std::move(copy);
		out += "\"";
	}
	namespace
	{
		void output(const SymbolData& Data, size_t pdbId, const std::vector<std::string>& pdbs)
		{
			Output Result(Data);
			Result.init();
			if (pdbId == 0)
			{
				Result.AddPdbIdsTable(pdbs);
			}
			Result.GenerateCommands(pdbId);
		}

		static std::string describe()
		{
			return
				"    opt: -outdir=<output directory>\n    opt: -nocolumnheaders\n     opt: -uselocaleseparator";
		}

	}

	OutputEngine CreateEngine()
	{
		OutputEngine res;
		res.name = "csv";
		res.output = &output;
		res.describe = &describe;
		return res;
	}

	namespace details
	{
		char getSeparator()
		{
			char SeparatorTmp = ',';
			if (!getFlag("-uselocaleseparator"))
			{
				return SeparatorTmp;
			}
			auto nRequestedChars = ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SLIST, nullptr, 0);
			if (2 == nRequestedChars) //we support only one char separator plus terminator (apparently possible up to tree)
			{
				std::vector<TCHAR> vSeparator(nRequestedChars, 0);
				if (0 != ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SLIST, &(vSeparator[0]), nRequestedChars))
				{
					SeparatorTmp = *std::begin(std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(vSeparator[0]));
				}
			}
			return SeparatorTmp;
		}
		const std::string& pdbNameColumn()
		{
			static const std::string pdbNameColumn("filename");
			return pdbNameColumn;
		}
		const std::string& pdbColumn()
		{
			static const std::string pdbColumn("pdbid");
			return pdbColumn;
		}
	};
}