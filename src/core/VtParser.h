#pragma once

// VtParser: xterm-compatible escape-sequence state machine. Feeds a Screen.
// UTF-8 is decoded here, so the Screen only ever sees codepoints.

#include <QByteArray>
#include <functional>
#include <string>
#include <vector>

class Screen;

class VtParser
{
public:
    explicit VtParser(Screen *screen);

    void setResponder(std::function<void(const QByteArray &)> fn) { m_respond = std::move(fn); }
    // OSC 7770: media requests from the `show` tool
    void setMediaHandler(std::function<void(const QByteArray &)> fn) { m_media = std::move(fn); }
    void feed(const char *data, int len);
    void reset();

private:
    enum class State { Ground, Esc, EscCharset, Csi, Osc, Ignore };

    void feedByte(unsigned char b);
    void ground(unsigned char b);
    void esc(unsigned char b);
    void csi(unsigned char b);
    void osc(unsigned char b);
    void executeControl(unsigned char b);
    void dispatchCsi(unsigned char final);
    void dispatchEsc(unsigned char final);
    void oscDispatch();
    void print(char32_t cp);
    int param(size_t idx, int def) const;
    void respond(const QByteArray &s);

    Screen *m_screen;
    State m_state = State::Ground;
    std::string m_csiRaw;
    std::string m_csiInter;
    std::vector<int> m_params;
    bool m_private = false;
    QByteArray m_osc;
    unsigned m_utf8Acc = 0;
    int m_utf8Need = 0;
    std::function<void(const QByteArray &)> m_respond;
    std::function<void(const QByteArray &)> m_media;
};
