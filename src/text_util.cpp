#include "text_util.h"

// CP437 byte for each UTF-8 "\xC3\x80".."\xC3\xBF" (U+00C0..U+00FF). Letters
// CP437 lacks fall back to their unaccented ASCII letter.
static const char LATIN1_TO_CP437[64] = {
    'A', 'A', 'A', 'A', '\x8E', '\x8F', '\x92', '\x80', // À Á Â Ã Ä Å Æ Ç
    'E', '\x90', 'E', 'E', 'I', 'I', 'I', 'I',          // È É Ê Ë Ì Í Î Ï
    'D', '\xA5', 'O', 'O', 'O', 'O', '\x99', 'x',       // Ð Ñ Ò Ó Ô Õ Ö ×
    'O', 'U', 'U', 'U', '\x9A', 'Y', 'P', '\xE1',       // Ø Ù Ú Û Ü Ý Þ ß
    '\x85', '\xA0', '\x83', 'a', '\x84', '\x86', '\x91', '\x87', // à á â ã ä å æ ç
    '\x8A', '\x82', '\x88', '\x89', '\x8D', '\xA1', '\x8C', '\x8B', // è é ê ë ì í î ï
    'd', '\xA4', '\x95', '\xA2', '\x93', 'o', '\x94', '\xF6', // ð ñ ò ó ô õ ö ÷
    'o', '\x97', '\xA3', '\x96', '\x81', 'y', 'p', '\x98',   // ø ù ú û ü ý þ ÿ
};

String toDisplayText(const String &utf8) {
  String out;
  out.reserve(utf8.length());
  size_t len = utf8.length();
  for (size_t i = 0; i < len;) {
    uint8_t c = utf8[i];
    if (c < 0x80) {
      out += (char)c;
      i++;
      continue;
    }

    uint8_t c1 = i + 1 < len ? (uint8_t)utf8[i + 1] : 0;
    uint8_t c2 = i + 2 < len ? (uint8_t)utf8[i + 2] : 0;
    if (c == 0xC3 && c1 >= 0x80 && c1 <= 0xBF) {
      out += LATIN1_TO_CP437[c1 - 0x80];
    } else if (c == 0xC2 && c1 == 0xB0) {
      out += TXT_DEGREE;
    } else if (c == 0xC2 && c1 == 0xB5) {
      out += TXT_MICRO;
    } else if (c == 0xC2 && c1 == 0xA0) {
      out += ' '; // non-breaking space
    } else if (c == 0xE2 && c1 == 0x80) {
      switch (c2) {
        case 0x98: case 0x99: out += '\''; break; // curly single quotes
        case 0x9C: case 0x9D: out += '"'; break;  // curly double quotes
        case 0x93: case 0x94: out += '-'; break;  // en/em dash
        case 0xA6: out += "..."; break;
        default: break;
      }
    } else if (c == 0xE2 && c1 == 0x82 && c2 == 0xAC) {
      out += "EUR"; // CP437 has no euro sign
    }

    // Skip the whole multi-byte sequence, whether or not it was mapped.
    i++;
    while (i < len && ((uint8_t)utf8[i] & 0xC0) == 0x80) {
      i++;
    }
  }
  return out;
}

String truncated(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) {
    return s;
  }
  return s.substring(0, maxLen - 1) + ".";
}

String fitText(const String &s, int16_t maxWidth, uint8_t textSize) {
  int maxChars = maxWidth / (6 * textSize);
  if (maxChars <= 0) {
    return "";
  }
  return truncated(s, maxChars);
}

String hhmm(int minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return String(buf);
}

String fmtNum(float v, int decimals) {
  if (isnan(v)) {
    return "--";
  }
  return String(v, decimals);
}
