#include "Signatures.h"

#include "../Memory/Memory.h"
#include "../../Core/Core.h"
#include <string>
#include <format>

CSignature::CSignature(const char* sDLLName, const char* sSignature, int8_t nOffset, const char* sName)
{
	m_dwVal = 0x0;
	m_sDLLName = sDLLName;
	m_sSignature = sSignature;
	m_nOffset = nOffset;
	m_sName = sName;

	U::Signatures.AddSignature(this);
}

CSignature::CSignature(const char* sDLLName, const char* sSignature, int8_t nOffset, const char* sName, int nReferenceOffset, const char* sReference)
	: CSignature(sDLLName, sSignature, nOffset, sName)
{
	m_nReferenceOffset = nReferenceOffset;
	m_sReference = sReference;
}

bool CSignature::Initialize()
{
	m_dwVal = m_sReference
		? U::Memory.FindSignatureWithReference(m_sDLLName, m_sSignature, m_nReferenceOffset, m_sReference)
		: U::Memory.FindSignature(m_sDLLName, m_sSignature);
	if (!m_dwVal)
	{
		if (m_sReference)
			U::Core.AppendFailText(std::format("CSignature::Initialize() failed to initialize:\n  {}\n  {}\n  {}\n  (reference \"{}\" at +{})", m_sName, m_sDLLName, m_sSignature, m_sReference, m_nReferenceOffset).c_str());
		else
			U::Core.AppendFailText(std::format("CSignature::Initialize() failed to initialize:\n  {}\n  {}\n  {}", m_sName, m_sDLLName, m_sSignature).c_str());
		return false;
	}

	m_dwVal += m_nOffset;
	return true;
}

bool CSignatures::Initialize()
{
	for (auto pSignature : m_vSignatures)
	{
		if (!pSignature)
			continue;

		if (!pSignature->Initialize())
			m_bFailed = true;
	}

	return !m_bFailed;
}