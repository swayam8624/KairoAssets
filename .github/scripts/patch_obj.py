from pathlib import Path
p = Path('OBJImporter.cppm')
s = p.read_text()
old = '''        [[nodiscard]] inline double ParseNumber(const Token& token, std::size_t line, std::string_view label)
        {
            double value = 0.0;
            const char* begin = token.Text.data();
            const char* end = begin + token.Text.size();
            const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
                Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
            return value;
        }'''
new = '''        [[nodiscard]] inline double ParseNumber(const Token& token, std::size_t line, std::string_view label)
        {
            const std::string_view text = token.Text;
            std::size_t cursor = 0u;
            bool negative = false;
            if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
            { negative = text[cursor] == '-'; ++cursor; }
            long double mantissa = 0.0L;
            std::size_t digits = 0u;
            while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
            { mantissa = mantissa * 10.0L + static_cast<int>(text[cursor] - '0'); ++cursor; ++digits; }
            int decimalDigits = 0;
            if (cursor < text.size() && text[cursor] == '.')
            {
                ++cursor;
                while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
                { mantissa = mantissa * 10.0L + static_cast<int>(text[cursor] - '0'); ++cursor; ++digits; ++decimalDigits; }
            }
            if (digits == 0u) Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
            int exponent = -decimalDigits;
            if (cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E'))
            {
                ++cursor;
                bool exponentNegative = false;
                if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-'))
                { exponentNegative = text[cursor] == '-'; ++cursor; }
                if (cursor == text.size() || text[cursor] < '0' || text[cursor] > '9')
                    Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
                int parsedExponent = 0;
                while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
                { parsedExponent = std::min(10000, parsedExponent * 10 + static_cast<int>(text[cursor] - '0')); ++cursor; }
                exponent += exponentNegative ? -parsedExponent : parsedExponent;
            }
            if (cursor != text.size() || exponent < -10000 || exponent > 10000)
                Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
            long double scaled = mantissa * std::pow(10.0L, static_cast<long double>(exponent));
            if (negative) scaled = -scaled;
            const double value = static_cast<double>(scaled);
            if (!std::isfinite(value)) Fail(line, token.Column, "invalid finite " + std::string(label) + ".");
            return value;
        }'''
if old not in s:
    raise SystemExit('OBJ ParseNumber pattern not found')
p.write_text(s.replace(old, new))
Path('.github/workflows/apply-portable-obj-parser-v2.yml').unlink()
Path('.github/workflows/apply-portable-obj-parser.yml').unlink(missing_ok=True)
Path('.github/scripts/patch_obj.py').unlink()
