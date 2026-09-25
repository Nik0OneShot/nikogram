#include "NetVars.h"

#include "../../SDK/Definitions/Interfaces/CHLClient.h"
#include "../Hash/FNV1A.h"
#include "../../Core/Core.h"
#include <format>

#ifdef GetProp
	#undef GetProp
#endif

// returns whether the netvar was found, so a genuine offset of 0 inside a data table isn't treated as missing
static bool FindOffset(RecvTable* pTable, uint32_t uHash, int& nOffset)
{
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
		{
			nOffset = pProp->GetOffset();
			return true;
		}

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (int nSubOffset = 0; FindOffset(pDataTable, uHash, nSubOffset))
			{
				nOffset = nSubOffset + pProp->GetOffset();
				return true;
			}
		}
	}

	return false;
}

int CNetVars::GetOffset(RecvTable* pTable, const char* szNetVar)
{
	int nOffset = 0;
	FindOffset(pTable, FNV1A::Hash32(szNetVar), nOffset);
	return nOffset;
}

int CNetVars::GetNetVar(const char* szClass, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szClass);
	for (auto pCurrNode = I::Client->GetAllClasses(); pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (uHash == FNV1A::Hash32(pCurrNode->m_pNetworkName))
		{
			int nOffset = 0;
			if (!FindOffset(pCurrNode->m_pRecvTable, FNV1A::Hash32(szNetVar), nOffset))
				U::Core.AppendFailText(std::format("CNetVars::GetNetVar() failed to find netvar:\n  {}\n  {}", szClass, szNetVar).c_str());
			return nOffset;
		}
	}

	U::Core.AppendFailText(std::format("CNetVars::GetNetVar() failed to find class:\n  {}\n  {}", szClass, szNetVar).c_str());
	return 0;
}

RecvProp* CNetVars::GetProp(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp;

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (pProp = GetProp(pDataTable, szNetVar))
				return pProp;
		}
	}

	return nullptr;
}

RecvProp* CNetVars::GetNetProp(const char* szClass, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szClass);
	for (auto pCurrNode = I::Client->GetAllClasses(); pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (uHash == FNV1A::Hash32(pCurrNode->m_pNetworkName))
			return GetProp(pCurrNode->m_pRecvTable, szNetVar);
	}

	return nullptr;
}