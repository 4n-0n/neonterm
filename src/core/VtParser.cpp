#include "core/VtParser.h"

#include "core/Screen.h"

#include <cstdlib>

VtParser::VtParser(Screen *screen)
    : m_screen(screen)
{
}

void VtParser::reset()
{
    m_state = State::Ground;
    m_csiRaw.clear();
    m_csiInter.clear();
    m_params.clear();
    m_private = false;
    m_osc.clear();
    m_utf8Acc = 0;
    m_utf8Need = 0;
}

void VtParser::feed(const char *data, int len)
{
    for (int i = 0; i < len; ++i)
        feedByte(static_cast<unsigned char>(data[i]));
}

void VtParser::feedByte(unsigned char b)
{
    // a partial UTF-8 sequence survives control bytes: only a non-continuation
    // byte aborts it (decoders would restart the sequence anyway)
    if (b == 0x1b) {
        // ESC after an OSC is the first half of ST: the string must be
        // dispatched before the Escape state takes over
        if (m_state == State::Osc)
            oscDispatch();
        m_state = State::Esc;
        m_utf8Need = 0;
        return;
    }
    if (b == 0x9c) {   // 8-bit ST
        m_state = State::Ground;
        return;
    }

    switch (m_state) {
    case State::Ground: ground(b); break;
    case State::Esc: esc(b); break;
    case State::EscCharset: m_state = State::Ground; break;
    case State::Csi: csi(b); break;
    case State::Osc: osc(b); break;
    case State::Ignore:
        if (b == 0x07)
            m_state = State::Ground;
        break;
    }
}

void VtParser::executeControl(unsigned char b)
{
    switch (b) {
    case 0x07: m_screen->ringBell(); break;
    case 0x08: m_screen->backspace(); break;
    case 0x09: m_screen->tab(); break;
    case 0x0a:
    case 0x0b:
    case 0x0c: m_screen->newline(); break;
    case 0x0d: m_screen->carriageReturn(); break;
    default: break;
    }
}

void VtParser::print(char32_t cp)
{
    m_screen->put(cp);
}

void VtParser::ground(unsigned char b)
{
    if (b >= 0x20 && b < 0x7f) {
        m_screen->put(char32_t(b));
        return;
    }
    if (b < 0x20) {
        executeControl(b);
        return;
    }
    // UTF-8
    if (m_utf8Need > 0) {
        if ((b & 0xc0) == 0x80) {
            m_utf8Acc = (m_utf8Acc << 6) | (b & 0x3f);
            if (--m_utf8Need == 0)
                print(char32_t(m_utf8Acc));
            return;
        }
        m_utf8Need = 0;
        m_utf8Acc = 0;
    }
    if (b >= 0xc2 && b <= 0xdf) {
        m_utf8Acc = b & 0x1f;
        m_utf8Need = 1;
    } else if (b >= 0xe0 && b <= 0xef) {
        m_utf8Acc = b & 0x0f;
        m_utf8Need = 2;
    } else if (b >= 0xf0 && b <= 0xf4) {
        m_utf8Acc = b & 0x07;
        m_utf8Need = 3;
    }
    // stray continuation byte / invalid lead: dropped
}

void VtParser::esc(unsigned char b)
{
    switch (b) {
    case '7': m_screen->saveCursor(); m_state = State::Ground; return;
    case '8': m_screen->restoreCursor(); m_state = State::Ground; return;
    case 'D': m_screen->newline(); m_state = State::Ground; return;
    case 'E': m_screen->nextLine(); m_state = State::Ground; return;
    case 'H': m_screen->setTabStop(m_screen->cursor().x); m_state = State::Ground; return;
    case 'M': m_screen->reverseIndex(); m_state = State::Ground; return;
    case 'c': m_screen->reset(); m_state = State::Ground; return;
    case '[':
        m_state = State::Csi;
        m_csiRaw.clear();
        m_csiInter.clear();
        m_params.clear();
        m_private = false;
        return;
    case ']':
        m_state = State::Osc;
        m_osc.clear();
        return;
    case 'P': case '^': case '_': case 'X':
        m_state = State::Ignore;
        return;
    case '(': case ')': case '*': case '+': case '-': case '.': case '/':
    case '#':
        m_state = State::EscCharset;
        return;
    case '\\':
        m_state = State::Ground;
        return;
    case '=': case '>': case 'N': case 'O': case 'Z': case '6': case '5': case '4':
        m_state = State::Ground;
        return;
    default:
        m_state = State::Ground;
        return;
    }
}

void VtParser::csi(unsigned char b)
{
    if (b >= 0x40 && b <= 0x7e) {
        dispatchCsi(b);
        m_state = State::Ground;
        return;
    }
    if (b >= 0x30 && b <= 0x3f) {
        m_csiRaw.push_back(char(b));
        return;
    }
    if (b >= 0x20 && b <= 0x2f) {
        m_csiInter.push_back(char(b));
        return;
    }
    if (b < 0x20) {
        executeControl(b);
        return;
    }
    // 0x7f ignored
}

int VtParser::param(size_t idx, int def) const
{
    if (idx >= m_params.size())
        return def;
    return m_params[idx];
}

void VtParser::dispatchCsi(unsigned char f)
{
    m_params.clear();
    m_private = !m_csiRaw.empty() && (m_csiRaw[0] == '?' || m_csiRaw[0] == '>'
                                      || m_csiRaw[0] == '<' || m_csiRaw[0] == '=');
    size_t start = (m_private || (!m_csiRaw.empty() && m_csiRaw[0] == ':')) ? 1 : 0;
    int cur = -1;
    for (size_t i = start; i < m_csiRaw.size(); ++i) {
        const char c = m_csiRaw[i];
        if (c >= '0' && c <= '9') {
            if (cur < 0)
                cur = 0;
            cur = cur * 10 + (c - '0');
            if (cur > 99999)
                cur = 99999;
        } else if (c == ';' || c == ':') {
            m_params.push_back(cur < 0 ? 0 : cur);
            cur = -1;
        }
    }
    if (cur >= 0)
        m_params.push_back(cur);
    else if (!m_csiRaw.empty() && (m_csiRaw.back() == ';'))
        m_params.push_back(0);

    auto p1 = [&](size_t i) { return std::max(1, param(i, 1)); };
    const int cw = m_screen->cols();
    const int ch = m_screen->rows();
    const Pos c = m_screen->cursor();

    switch (f) {
    case 'A': m_screen->moveRel(0, -p1(0)); break;
    case 'B': m_screen->moveRel(0, p1(0)); break;
    case 'C': m_screen->moveRel(p1(0), 0); break;
    case 'D': m_screen->moveRel(-p1(0), 0); break;
    case 'E': m_screen->moveRel(0, p1(0)); m_screen->setCol(0); break;
    case 'F': m_screen->moveRel(0, -p1(0)); m_screen->setCol(0); break;
    case 'G':
    case '`': m_screen->setCol(p1(0) - 1); break;
    case 'H':
    case 'f': m_screen->moveTo(p1(1) - 1, p1(0) - 1); break;
    case 'I': m_screen->tab(p1(0)); break;
    case 'J': m_screen->eraseInDisplay(param(0, 0)); break;
    case 'K': m_screen->eraseInLine(param(0, 0)); break;
    case 'L': m_screen->insertLines(p1(0)); break;
    case 'M': m_screen->deleteLines(p1(0)); break;
    case 'P': m_screen->deleteChars(p1(0)); break;
    case 'S': m_screen->scrollUp(p1(0)); break;
    case 'T': m_screen->scrollDown(p1(0)); break;
    case 'X': m_screen->eraseChars(p1(0)); break;
    case 'Z': m_screen->tabBack(p1(0)); break;
    case '@': m_screen->insertChars(p1(0)); break;
    case 'a': m_screen->moveRel(p1(0), 0); break;
    case 'd': m_screen->setRow(p1(0) - 1); break;
    case 'e': m_screen->moveRel(0, p1(0)); break;
    case 'g': m_screen->tabClear(param(0, 0)); break;
    case 'h':
        for (int v : m_params) {
            if (m_private)
                m_screen->setPrivateMode(v, true);
            else
                m_screen->setMode(v, true);
        }
        break;
    case 'l':
        for (int v : m_params) {
            if (m_private)
                m_screen->setPrivateMode(v, false);
            else
                m_screen->setMode(v, false);
        }
        break;
    case 'm': m_screen->applySgr(m_params); break;
    case 'n':
        if (param(0, 0) == 5)
            respond("\x1b[0n");
        else if (param(0, 0) == 6)
            respond("\x1b[" + QByteArray::number(c.y + 1) + ";"
                    + QByteArray::number(c.x + 1) + "R");
        break;
    case 'c':
        if (m_private && param(0, 0) == 0)
            respond("\x1b[>0;276;0c");
        else
            respond("\x1b[?62;22c");
        break;
    case 'q':
        if (!m_csiInter.empty() && m_csiInter[0] == ' ')
            m_screen->setCursorStyle(param(0, 0));
        break;
    case 'r': {
        int top = std::max(1, param(0, 1)) - 1;
        int bottom = param(1, ch) - 1;
        if (bottom < 0)
            bottom = ch - 1;
        if (top == 0 && bottom == ch - 1)
            m_screen->resetScrollRegion();
        else
            m_screen->setScrollRegion(top, bottom);
        break;
    }
    case 's': m_screen->saveCursor(); break;
    case 'u': m_screen->restoreCursor(); break;
    case 't': break;   // window ops: ignored on purpose
    default: break;
    }
    (void)cw;
}

void VtParser::respond(const QByteArray &s)
{
    if (m_respond)
        m_respond(s);
}

void VtParser::osc(unsigned char b)
{
    if (b == 0x07) {
        oscDispatch();
        m_state = State::Ground;
        return;
    }
    if (b == 0x1b) {
        oscDispatch();
        m_state = State::Esc;   // the following '\\' ends the string
        return;
    }
    if (m_osc.size() < 8192)
        m_osc.append(char(b));
}

void VtParser::oscDispatch()
{
    const QByteArray payload = m_osc;
    m_osc.clear();

    const int semi = payload.indexOf(';');
    if (semi <= 0)
        return;
    bool ok = false;
    const int cmd = payload.left(semi).toInt(&ok);
    if (!ok)
        return;
    if (cmd == 0 || cmd == 1 || cmd == 2) {
        m_screen->setTitle(QString::fromUtf8(payload.mid(semi + 1)));
        m_screen->touch();
        return;
    }
    if (cmd == 7770) {
        if (m_media)
            m_media(payload.mid(semi + 1));
        return;
    }
}
