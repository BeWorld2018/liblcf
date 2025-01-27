/*
 * This file is part of liblcf. Copyright (c) 2021 liblcf authors.
 * https://github.com/EasyRPG/liblcf - https://easyrpg.org
 *
 * liblcf is Free/Libre Open Source Software, released under the MIT License.
 * For the full copyright and license information, please view the COPYING
 * file that was distributed with this source code.
 */

#include "lcf/config.h"
#include "lcf/scope_guard.h"

#if LCF_SUPPORT_ICU
#   include <unicode/ucsdet.h>
#   include <unicode/ucnv.h>
#   include <unicode/normalizer2.h>
#   include <unicode/unistr.h>
#else
#   ifdef _MSC_VER
#		error MSVC builds require ICU
#	endif
#endif

#ifdef _WIN32
#   include <windows.h>
#else
#   if !LCF_SUPPORT_ICU
#       include <iconv.h>
#   endif
#   include <locale>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "lcf/inireader.h"
#include "lcf/ldb/reader.h"
#include "lcf/reader_util.h"

namespace lcf {

namespace ReaderUtil {
}

std::string ReaderUtil::CodepageToEncoding(int codepage) {
	if (codepage == 0)
		return std::string();

	if (codepage == 932) {
#if LCF_SUPPORT_ICU
		return "ibm-943_P15A-2003";
#else
		return "SHIFT_JIS";
#endif
	}
	if (codepage == 949) {
#if LCF_SUPPORT_ICU
		return "windows-949-2000";
#else
		return "cp949";
#endif
	}
	std::ostringstream out;
#if LCF_SUPPORT_ICU
	out << "windows-" << codepage;
#else
	out << "CP" << codepage;
#endif

	// Looks like a valid codepage
	std::string outs = out.str();
	return outs;
}

std::string ReaderUtil::DetectEncoding(lcf::rpg::Database& db) {
	std::vector<std::string> encodings = DetectEncodings(db);

	if (encodings.empty()) {
		return "";
	}

	return encodings.front();
}

std::vector<std::string> ReaderUtil::DetectEncodings(lcf::rpg::Database& db) {
#if LCF_SUPPORT_ICU
	std::ostringstream text;

	auto append = [](const auto& s) {
		return ToString(s) + " ";
	};

	lcf::rpg::ForEachString(db.system, [&](const auto& val, const auto& ctx) {
		text << append(val);
	});

	// Cannot use ForEachString here for Terms:
	// Too much untranslated garbage data in there, even in default database
	for (const auto& s: {
		db.terms.menu_save,
		db.terms.menu_quit,
		db.terms.new_game,
		db.terms.load_game,
		db.terms.exit_game,
		db.terms.status,
		db.terms.row,
		db.terms.order,
		db.terms.wait_on,
		db.terms.wait_off,
		db.terms.level,
		db.terms.health_points,
		db.terms.spirit_points,
		db.terms.normal_status,
		db.terms.sp_cost,
		db.terms.attack,
		db.terms.defense,
		db.terms.spirit,
		db.terms.agility,
		db.terms.weapon,
		db.terms.shield,
		db.terms.armor,
		db.terms.helmet,
		db.terms.accessory,
		db.terms.save_game_message,
		db.terms.load_game_message,
		db.terms.exit_game_message,
		db.terms.file,
		db.terms.yes,
		db.terms.no
	}) {
		text << append(s);
	}

	return ReaderUtil::DetectEncodings(text.str());
#else
	return std::vector<std::string>();
#endif
}

std::string ReaderUtil::DetectEncoding(StringView string) {
	std::vector<std::string> encodings = DetectEncodings(string);

	if (encodings.empty()) {
		return "";
	}

	return encodings.front();
}

std::vector<std::string> ReaderUtil::DetectEncodings(StringView string) {
std::vector<std::string> encodings;
#if LCF_SUPPORT_ICU
	if (!string.empty()) {
		UErrorCode status = U_ZERO_ERROR;
		UCharsetDetector* detector = ucsdet_open(&status);

		auto s = std::string(string);
		ucsdet_setText(detector, s.c_str(), s.length(), &status);

		int32_t matches_count;
		const UCharsetMatch** matches = ucsdet_detectAll(detector, &matches_count, &status);

		if (matches != nullptr) {
			// Collect all candidates, most confident comes first
			for (int i = 0; i < matches_count; ++i) {
				std::string encoding = ucsdet_getName(matches[i], &status);

				// Fixes to ensure proper Windows encodings
				if (encoding == "Shift_JIS") {
					encodings.emplace_back("ibm-943_P15A-2003"); // Japanese with \ as backslash
				} else if (encoding == "EUC-KR") {
					encodings.emplace_back("windows-949-2000"); // Korean with \ as backlash
				} else if (encoding == "GB18030") {
					encodings.emplace_back("windows-936-2000"); // Simplified Chinese
				} else if (encoding == "ISO-8859-1" || encoding == "windows-1252") {
					encodings.emplace_back("ibm-5348_P100-1997"); // Occidental with Euro
				} else if (encoding == "ISO-8859-2" || encoding == "windows-1250") {
					encodings.emplace_back("ibm-5346_P100-1998"); // Central Europe with Euro
				} else if (encoding == "ISO-8859-5" || encoding == "windows-1251") {
					encodings.emplace_back("ibm-5347_P100-1998"); // Cyrillic with Euro
				} else if (encoding == "ISO-8859-6" || encoding == "windows-1256") {
					encodings.emplace_back("ibm-9448_X100-2005"); // Arabic with Euro + 8 chars
				} else if (encoding == "ISO-8859-7" || encoding == "windows-1253") {
					encodings.emplace_back("ibm-5349_P100-1998"); // Greek with Euro
				} else if (encoding == "ISO-8859-8" || encoding == "windows-1255") {
					encodings.emplace_back("ibm-9447_P100-2002"); // Hebrew with Euro
				} else {
					encodings.push_back(encoding);
				}
			}
		}
		ucsdet_close(detector);
	}
#endif

	return encodings;
}

std::string ReaderUtil::GetEncoding(StringView ini_file) {
	INIReader ini(ToString(ini_file));
	if (ini.ParseError() != -1) {
		std::string encoding = ini.Get("EasyRPG", "Encoding", std::string());
		if (!encoding.empty()) {
			return ReaderUtil::CodepageToEncoding(atoi(encoding.c_str()));
		}
	}
	return std::string();
}

std::string ReaderUtil::GetEncoding(std::istream& filestream) {
	INIReader ini(filestream);
	if (ini.ParseError() != -1) {
		std::string encoding = ini.Get("EasyRPG", "Encoding", std::string());
		if (!encoding.empty()) {
			return ReaderUtil::CodepageToEncoding(atoi(encoding.c_str()));
		}
	}
	return std::string();
}

std::string ReaderUtil::GetLocaleEncoding() {
#ifdef _WIN32
	int codepage = GetACP();
#elif __ANDROID__
	// No std::locale support in NDK
	// Doesn't really matter because the Android version auto-detects via ICU
	int codepage = 1252;
#else
	int codepage = 1252;

	std::locale loc = std::locale("");
	// Gets the language and culture part only
	std::string loc_full = loc.name().substr(0, loc.name().find_first_of("@."));
	// Gets the language part only
	std::string loc_lang = loc.name().substr(0, loc.name().find_first_of("_"));

	if      (loc_lang == "th")    codepage = 874;
	else if (loc_lang == "ja")    codepage = 932;
	else if (loc_full == "zh_CN" ||
	         loc_full == "zh_SG") codepage = 936;
	else if (loc_lang == "ko")    codepage = 949;
	else if (loc_full == "zh_TW" ||
	         loc_full == "zh_HK") codepage = 950;
	else if (loc_lang == "cs" ||
	         loc_lang == "hu" ||
	         loc_lang == "pl" ||
	         loc_lang == "ro" ||
	         loc_lang == "hr" ||
	         loc_lang == "sk" ||
	         loc_lang == "sl")    codepage = 1250;
	else if (loc_lang == "ru")    codepage = 1251;
	else if (loc_lang == "ca" ||
	         loc_lang == "da" ||
	         loc_lang == "de" ||
	         loc_lang == "en" ||
	         loc_lang == "es" ||
	         loc_lang == "fi" ||
	         loc_lang == "fr" ||
	         loc_lang == "it" ||
	         loc_lang == "nl" ||
	         loc_lang == "nb" ||
	         loc_lang == "pt" ||
	         loc_lang == "sv" ||
	         loc_lang == "eu")    codepage = 1252;
	else if (loc_lang == "el")    codepage = 1253;
	else if (loc_lang == "tr")    codepage = 1254;
	else if (loc_lang == "he")    codepage = 1255;
	else if (loc_lang == "ar")    codepage = 1256;
	else if (loc_lang == "et" ||
	         loc_lang == "lt" ||
	         loc_lang == "lv")    codepage = 1257;
	else if (loc_lang == "vi")    codepage = 1258;
#endif

	return CodepageToEncoding(codepage);
}

std::string ReaderUtil::Recode(StringView str_to_encode, StringView source_encoding) {
	return ReaderUtil::Recode(str_to_encode, source_encoding, "UTF-8");
}

std::string ReaderUtil::Recode(StringView str_to_encode,
                               StringView src_enc,
                               StringView dst_enc) {

	if (src_enc.empty() || dst_enc.empty() || str_to_encode.empty()) {
		return ToString(str_to_encode);
	}

	auto src_cp = SvAtoi(src_enc);
	const auto& src_enc_str = src_cp > 0
		? ReaderUtil::CodepageToEncoding(src_cp)
		: ToString(src_enc);

	auto dst_cp = SvAtoi(dst_enc);
	const auto& dst_enc_str = dst_cp > 0
		? ReaderUtil::CodepageToEncoding(dst_cp)
		: ToString(dst_enc);

#if LCF_SUPPORT_ICU
	auto status = U_ZERO_ERROR;
	auto conv_from = ucnv_open(src_enc_str.c_str(), &status);

	if (status != U_ZERO_ERROR && status != U_AMBIGUOUS_ALIAS_WARNING) {
		fprintf(stderr, "liblcf:  ucnv_open() error for source encoding \"%s\": %s\n", src_enc_str.c_str(), u_errorName(status));
		return std::string();
	}
	status = U_ZERO_ERROR;
	auto conv_from_sg = makeScopeGuard([&]() { ucnv_close(conv_from); });

	auto conv_to = ucnv_open(dst_enc_str.c_str(), &status);

	if (status != U_ZERO_ERROR && status != U_AMBIGUOUS_ALIAS_WARNING) {
		fprintf(stderr, "liblcf:  ucnv_open() error for dest encoding \"%s\": %s\n", dst_enc_str.c_str(), u_errorName(status));
		return std::string();
	}
	auto conv_to_sg = makeScopeGuard([&]() { ucnv_close(conv_to); });
	status = U_ZERO_ERROR;

	std::string result(str_to_encode.size() * 4, '\0');
	auto* src = str_to_encode.data();
	auto* dst = &result.front();

	ucnv_convertEx(conv_to, conv_from,
			&dst, dst + result.size(),
			&src, src + str_to_encode.size(),
			nullptr, nullptr, nullptr, nullptr,
			true, true,
			&status);

	if (U_FAILURE(status)) {
		fprintf(stderr, "liblcf: ucnv_convertEx() error when encoding \"%.*s\": %s\n", (int)str_to_encode.length(), str_to_encode.data(), u_errorName(status));
		return std::string();
	}

	result.resize(dst - result.c_str());
	result.shrink_to_fit();

	return result;
#else
	iconv_t cd = iconv_open(dst_enc_str.c_str(), src_enc_str.c_str());
	if (cd == (iconv_t)-1)
		return ToString(str_to_encode);
	char *src = const_cast<char *>(str_to_encode.data());
	size_t src_left = str_to_encode.size();
	size_t dst_size = str_to_encode.size() * 5 + 10;
	char *dst = new char[dst_size];
	size_t dst_left = dst_size;
#    ifdef ICONV_CONST
	char ICONV_CONST *p = src;
#    else
	char *p = src;
#    endif
	char *q = dst;
	size_t status = iconv(cd, &p, &src_left, &q, &dst_left);
	iconv_close(cd);
	if (status == (size_t) -1 || src_left > 0) {
		delete[] dst;
		return std::string();
	}
	*q++ = '\0';
	std::string result(dst);
	delete[] dst;
	return result;
#endif
}

std::string ReaderUtil::Normalize(StringView str) {
#if LCF_SUPPORT_ICU
	icu::UnicodeString uni = icu::UnicodeString(str.data(), str.length(), "utf-8").toLower();
	UErrorCode err = U_ZERO_ERROR;
	std::string res;
	const icu::Normalizer2* norm = icu::Normalizer2::getNFKCInstance(err);
	if (U_FAILURE(err)) {
		static bool err_reported = false;
		if (!err_reported) {
			fprintf(stderr, "Normalizer2::getNFKCInstance failed (%s). \"nrm\" is probably missing in the ICU data file. Unicode normalization will not work!\n", u_errorName(err));
			err_reported = true;
		}
		uni.toUTF8String(res);
		return res;
	}
	icu::UnicodeString f = norm->normalize(uni, err);
	if (U_FAILURE(err)) {
		uni.toUTF8String(res);
	} else {
		f.toUTF8String(res);
	}
	return res;
#else
	auto result = std::string(str);
	std::transform(result.begin(), result.end(), result.begin(), tolower);
	return result;
#endif
}

} //namespace lcf
