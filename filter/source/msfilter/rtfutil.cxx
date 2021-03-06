/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <filter/msfilter/rtfutil.hxx>
#include <rtl/strbuf.hxx>
#include <osl/diagnose.h>
#include <svtools/rtfkeywd.hxx>
#include <rtl/character.hxx>
#include <tools/stream.hxx>

namespace msfilter
{
namespace rtfutil
{
OString OutHex(sal_uLong nHex, sal_uInt8 nLen)
{
    sal_Char aNToABuf[] = "0000000000000000";

    OSL_ENSURE(nLen < sizeof(aNToABuf), "nLen is too big");
    if (nLen >= sizeof(aNToABuf))
        nLen = (sizeof(aNToABuf) - 1);

    // Set pointer to the buffer end
    sal_Char* pStr = aNToABuf + (sizeof(aNToABuf) - 1);
    for (sal_uInt8 n = 0; n < nLen; ++n)
    {
        *(--pStr) = static_cast<sal_Char>(nHex & 0xf) + 48;
        if (*pStr > '9')
            *pStr += 39;
        nHex >>= 4;
    }
    return OString(pStr);
}

// Ideally, this function should work on (sal_uInt32) Unicode scalar values
// instead of (sal_Unicode) UTF-16 code units.  However, at least "Rich Text
// Format (RTF) Specification Version 1.9.1" available at
// <https://www.microsoft.com/en-us/download/details.aspx?id=10725> does not
// look like it allows non-BMP Unicode characters >= 0x10000 in the \uN notation
// (it only talks about "Unicode character", but then explains how values of N
// greater than 32767 will be expressed as negative signed 16-bit numbers, so
// that smells like \uN is limited to BMP).
// However the "Mathematics" section has an example that shows the code point
// U+1D44E being encoded as UTF-16 surrogate pair "\u-10187?\u-9138?", so
// sal_Unicode actually works fine here.
OString OutChar(sal_Unicode c, int* pUCMode, rtl_TextEncoding eDestEnc, bool* pSuccess,
                bool bUnicode)
{
    if (pSuccess)
        *pSuccess = true;
    OStringBuffer aBuf;
    const sal_Char* pStr = nullptr;
    // 0x0b instead of \n, etc because of the replacements in SwWW8AttrIter::GetSnippet()
    switch (c)
    {
        case 0x0b:
            // hard line break
            pStr = OOO_STRING_SVTOOLS_RTF_LINE;
            break;
        case '\t':
            pStr = OOO_STRING_SVTOOLS_RTF_TAB;
            break;
        case '\\':
        case '}':
        case '{':
            aBuf.append('\\');
            aBuf.append(static_cast<sal_Char>(c));
            break;
        case 0xa0:
            // non-breaking space
            pStr = "\\~";
            break;
        case 0x1e:
            // non-breaking hyphen
            pStr = "\\_";
            break;
        case 0x1f:
            // optional hyphen
            pStr = "\\-";
            break;
        default:
            if (c >= ' ' && c <= '~')
                aBuf.append(static_cast<sal_Char>(c));
            else
            {
                OUString sBuf(&c, 1);
                OString sConverted;
                if (pSuccess)
                    *pSuccess &= sBuf.convertToString(&sConverted, eDestEnc,
                                                      RTL_UNICODETOTEXT_FLAGS_UNDEFINED_ERROR
                                                          | RTL_UNICODETOTEXT_FLAGS_INVALID_ERROR);
                else
                    sBuf.convertToString(&sConverted, eDestEnc, OUSTRING_TO_OSTRING_CVTFLAGS);
                const sal_Int32 nLen = sConverted.getLength();

                if (pUCMode && bUnicode)
                {
                    if (*pUCMode != nLen)
                    {
                        aBuf.append("\\uc");
                        aBuf.append(static_cast<sal_Int32>(nLen));
                        // #i47831# add an additional whitespace, so that "document whitespaces" are not ignored.
                        aBuf.append(' ');
                        *pUCMode = nLen;
                    }
                    aBuf.append("\\u");
                    aBuf.append(static_cast<sal_Int32>(c));
                }

                for (sal_Int32 nI = 0; nI < nLen; ++nI)
                {
                    aBuf.append("\\'");
                    aBuf.append(OutHex(sConverted[nI], 2));
                }
            }
    }
    if (pStr)
    {
        aBuf.append(pStr);
        switch (c)
        {
            case 0xa0:
            case 0x1e:
            case 0x1f:
                break;
            default:
                aBuf.append(' ');
        }
    }
    return aBuf.makeStringAndClear();
}

OString OutString(const OUString& rStr, rtl_TextEncoding eDestEnc, bool bUnicode)
{
    SAL_INFO("filter.ms", OSL_THIS_FUNC << ", rStr = '" << rStr << "'");
    OStringBuffer aBuf;
    int nUCMode = 1;
    for (sal_Int32 n = 0; n < rStr.getLength(); ++n)
        aBuf.append(OutChar(rStr[n], &nUCMode, eDestEnc, nullptr, bUnicode));
    if (nUCMode != 1)
    {
        aBuf.append(OOO_STRING_SVTOOLS_RTF_UC);
        aBuf.append(sal_Int32(1));
        aBuf.append(
            " "); // #i47831# add an additional whitespace, so that "document whitespaces" are not ignored.;
    }
    return aBuf.makeStringAndClear();
}

/// Checks if lossless conversion of the string to eDestEnc is possible or not.
static bool TryOutString(const OUString& rStr, rtl_TextEncoding eDestEnc)
{
    int nUCMode = 1;
    for (sal_Int32 n = 0; n < rStr.getLength(); ++n)
    {
        bool bRet;
        OutChar(rStr[n], &nUCMode, eDestEnc, &bRet);
        if (!bRet)
            return false;
    }
    return true;
}

OString OutStringUpr(const sal_Char* pToken, const OUString& rStr, rtl_TextEncoding eDestEnc)
{
    if (TryOutString(rStr, eDestEnc))
        return OString("{") + pToken + " " + OutString(rStr, eDestEnc) + "}";

    OStringBuffer aRet;
    aRet.append("{" OOO_STRING_SVTOOLS_RTF_UPR "{");
    aRet.append(pToken);
    aRet.append(" ");
    aRet.append(OutString(rStr, eDestEnc, /*bUnicode =*/false));
    aRet.append("}{" OOO_STRING_SVTOOLS_RTF_IGNORE OOO_STRING_SVTOOLS_RTF_UD "{");
    aRet.append(pToken);
    aRet.append(" ");
    aRet.append(OutString(rStr, eDestEnc));
    aRet.append("}}}");
    return aRet.makeStringAndClear();
}

int AsHex(char ch)
{
    int ret = 0;
    if (rtl::isAsciiDigit(static_cast<unsigned char>(ch)))
        ret = ch - '0';
    else
    {
        if (ch >= 'a' && ch <= 'f')
            ret = ch - 'a';
        else if (ch >= 'A' && ch <= 'F')
            ret = ch - 'A';
        else
            return -1;
        ret += 10;
    }
    return ret;
}

OString WriteHex(const sal_uInt8* pData, sal_uInt32 nSize, SvStream* pStream, sal_uInt32 nLimit)
{
    OStringBuffer aRet;

    sal_uInt32 nBreak = 0;
    for (sal_uInt32 i = 0; i < nSize; i++)
    {
        OString sNo = OString::number(pData[i], 16);
        if (sNo.getLength() < 2)
        {
            if (pStream)
                pStream->WriteChar('0');
            else
                aRet.append('0');
        }
        if (pStream)
            pStream->WriteCharPtr(sNo.getStr());
        else
            aRet.append(sNo);
        if (++nBreak == nLimit)
        {
            if (pStream)
                pStream->WriteCharPtr(SAL_NEWLINE_STRING);
            else
                aRet.append(SAL_NEWLINE_STRING);
            nBreak = 0;
        }
    }

    return aRet.makeStringAndClear();
}

bool ExtractOLE2FromObjdata(const OString& rObjdata, SvStream& rOle2)
{
    SvMemoryStream aStream;
    int b = 0, count = 2;

    // Feed the destination text to a stream.
    for (int i = 0; i < rObjdata.getLength(); ++i)
    {
        char ch = rObjdata[i];
        if (ch != 0x0d && ch != 0x0a)
        {
            b = b << 4;
            sal_Int8 parsed = msfilter::rtfutil::AsHex(ch);
            if (parsed == -1)
                return false;
            b += parsed;
            count--;
            if (!count)
            {
                aStream.WriteChar(b);
                count = 2;
                b = 0;
            }
        }
    }

    // Skip ObjectHeader, see [MS-OLEDS] 2.2.4.
    if (aStream.Tell())
    {
        aStream.Seek(0);
        sal_uInt32 nData;
        aStream.ReadUInt32(nData); // OLEVersion
        aStream.ReadUInt32(nData); // FormatID
        aStream.ReadUInt32(nData); // ClassName
        aStream.SeekRel(nData);
        aStream.ReadUInt32(nData); // TopicName
        aStream.SeekRel(nData);
        aStream.ReadUInt32(nData); // ItemName
        aStream.SeekRel(nData);
        aStream.ReadUInt32(nData); // NativeDataSize

        if (nData)
        {
            rOle2.WriteStream(aStream);
            rOle2.Seek(0);
        }
    }

    return true;
}
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
