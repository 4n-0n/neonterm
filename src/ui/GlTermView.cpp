#include "ui/GlTermView.h"

#include "core/Session.h"
#include "ui/MediaStore.h"
#include "ui/NeonFont.h"
#include "ui/Theme.h"

#include <QElapsedTimer>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPainter>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace {

const char *kVsGlyph = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aCell;      // col, row, glyphIndex, width in cells
layout(location = 2) in vec4 aColor;
uniform vec2 uCell;
uniform vec2 uOrigin;
uniform vec2 uViewport;
uniform vec2 uTiles;
uniform float uTileW;
uniform float uTileH;
uniform vec2 uAtlasSize;
out vec2 vUV;
out vec4 vColor;
void main()
{
    float gi = aCell.z;
    vec2 tile = vec2(mod(gi, uTiles.x), floor(gi / uTiles.x));
    vec2 uv0 = vec2(tile.x * uTileW, tile.y * uTileH) / uAtlasSize;
    vec2 span = vec2(aCell.w * uCell.x, uCell.y) / uAtlasSize;
    vUV = uv0 + aCorner * span;
    vec2 size = vec2(aCell.w * uCell.x, uCell.y);
    vec2 px = uOrigin + vec2(aCell.x * uCell.x, aCell.y * uCell.y) + aCorner * size;
    gl_Position = vec4(px.x / uViewport.x * 2.0 - 1.0, 1.0 - px.y / uViewport.y * 2.0, 0.0, 1.0);
    vColor = aColor;
}
)";

const char *kFsGlyph = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main()
{
    float a = texture(uAtlas, vUV).r;
    if (a <= 0.004)
        discard;
    fragColor = vec4(vColor.rgb, vColor.a * a);
}
)";

const char *kVsStroke = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aSeg;      // x0, y0, x1, y1 in glyph units
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aExtra;    // x = stroke thickness in px, yz = cell
uniform vec2 uCell;
uniform vec2 uOrigin;
uniform vec2 uViewport;
uniform vec2 uGlyphOff;
uniform float uScale;
out vec4 vColor;
out float vSide;
out float vHalf;
void main()
{
    vec2 cell = vec2(aExtra.y, aExtra.z);
    vec2 p0 = uOrigin + cell * uCell + uGlyphOff + aSeg.xy * uScale;
    vec2 p1 = uOrigin + cell * uCell + uGlyphOff + aSeg.zw * uScale;
    vec2 d = p1 - p0;
    float len = length(d);
    vec2 dir = len > 0.0001 ? d / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    float h = aExtra.x * 0.5;
    vec2 base = mix(p0, p1, aCorner.x);
    vec2 pos = base + dir * (aCorner.x * 2.0 - 1.0) * h + nrm * (aCorner.y * 2.0 - 1.0) * h;
    vSide = (aCorner.y * 2.0 - 1.0) * h;
    vHalf = h;
    gl_Position = vec4(pos.x / uViewport.x * 2.0 - 1.0, 1.0 - pos.y / uViewport.y * 2.0, 0.0, 1.0);
    vColor = aColor;
}
)";

const char *kFsStroke = R"(#version 330 core
in vec4 vColor;
in float vSide;
in float vHalf;
out vec4 fragColor;
void main()
{
    // analytic coverage across the stroke: 1px antialiased edge, no MSAA needed
    float a = clamp(vHalf + 0.5 - abs(vSide), 0.0, 1.0);
    if (a <= 0.004)
        discard;
    fragColor = vec4(vColor.rgb, vColor.a * a);
}
)";

const char *kVsMedia = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uCell;
uniform vec2 uOrigin;
uniform vec2 uViewport;
uniform vec4 uRect;      // x, y, w, h in cells
out vec2 vUV;
void main()
{
    vec2 px = uOrigin + vec2(uRect.x * uCell.x, uRect.y * uCell.y)
              + aPos * vec2(uRect.z * uCell.x, uRect.w * uCell.y);
    vUV = aPos;
    gl_Position = vec4(px.x / uViewport.x * 2.0 - 1.0, 1.0 - px.y / uViewport.y * 2.0, 0.0, 1.0);
}
)";

const char *kFsMedia = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUV); }
)";

const char *kVsQuad = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aRect;      // x, y, w, h in cells
layout(location = 2) in vec4 aColor;
uniform vec2 uCell;
uniform vec2 uOrigin;
uniform vec2 uViewport;
out vec4 vColor;
void main()
{
    vec2 px = uOrigin + vec2(aRect.x * uCell.x, aRect.y * uCell.y)
              + aCorner * vec2(aRect.z * uCell.x, aRect.w * uCell.y);
    gl_Position = vec4(px.x / uViewport.x * 2.0 - 1.0, 1.0 - px.y / uViewport.y * 2.0, 0.0, 1.0);
    vColor = aColor;
}
)";

const char *kFsQuad = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

const char *kVsFull = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main()
{
    vUV = vec2(aPos.x, 1.0 - aPos.y);
    gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);
}
)";

const char *kFsFinal = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float uCrt;
out vec4 fragColor;

void main()
{
    vec3 col = texture(uScene, vUV).rgb;

    float scan = 0.5 + 0.5 * sin(gl_FragCoord.y * 6.2831853 / 3.0);
    col *= mix(1.0, 0.94 + 0.06 * scan, uCrt);

    vec2 q = vUV * 2.0 - 1.0;
    float vig = clamp(1.0 - 0.16 * dot(q, q) - 0.10 * pow(abs(q.y), 3.0), 0.0, 1.0);
    col *= mix(1.0, vig, uCrt);

    fragColor = vec4(col, 1.0);
}
)";

QColor toColor(QRgb c, int alpha = 255)
{
    QColor q = QColor::fromRgb(c);
    q.setAlpha(alpha);
    return q;
}

int widthCellsOf(const std::vector<Cell> &cells, int c, int limit)
{
    return (c + 1 < limit && (cells[size_t(c + 1)].flags & CF_Cont)) ? 2 : 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Canvas: the QOpenGLWidget child. It reads the surface state through
// paintState() and never owns the session, the selection or the keyboard.
// ---------------------------------------------------------------------------
class GlTermView::Canvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
public:
    explicit Canvas(GlTermView *view)
        : QOpenGLWidget(view)
        , m_view(view)
    {
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    ~Canvas() override
    {
        if (!m_ready || !context())
            return;
        makeCurrent();
        releaseGl();
        doneCurrent();
    }

    struct Inst {
        float a = 0, b = 0, c = 0, d = 0;
        float r = 0, g = 0, bl = 0, al = 1;
        float e = 0, f = 0, g2 = 0, h2 = 0;   // thickness, cell col/row for strokes
    };

    bool ready() const { return m_ready; }
    bool strokeFont = false;
    QString rendererName() const { return m_renderer; }
    int lastFrameUs() const { return m_lastFrameUs; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void releaseGl();
    void buildPrograms();
    void ensureTargets(int pw, int ph);
    bool buildAtlas();
    void uploadAtlas();
    void buildFrame();
    void drawStrokes();
    void drawMedia(const TermSurface::PaintState &st);
    void buildMediaQuads(const TermSurface::PaintState &st, int topAbs);
    void uploadAndDraw(QOpenGLShaderProgram *prog, const QVector<Inst> &inst, int count,
                       bool textured);
    int glyphFor(char32_t cp, int style, int widthCells);
    void addQuad(QVector<Inst> &out, double x, double y, double w, double h, const QColor &col);
    void drawFullscreen(QOpenGLShaderProgram *prog, GLuint tex, GLuint tex2);

    GlTermView *m_view = nullptr;

    bool m_ready = false;
    QString m_renderer;

    QOpenGLShaderProgram *m_quadProg = nullptr;
    QOpenGLShaderProgram *m_strokeProg = nullptr;
    QOpenGLShaderProgram *m_mediaProg = nullptr;
    QOpenGLShaderProgram *m_glyphProg = nullptr;
    QOpenGLShaderProgram *m_finalProg = nullptr;

    QOpenGLTexture *m_atlasTex = nullptr;
    QOpenGLFramebufferObject *m_scene = nullptr;

    GLuint m_vao = 0;
    GLuint m_cornerVbo = 0;
    GLuint m_instVbo = 0;
    GLuint m_fsVao = 0;
    GLuint m_fsVbo = 0;

    QVector<Inst> m_bg;
    QVector<Inst> m_glyphs;
    QVector<Inst> m_strokes;
    struct MediaQuad { int id = 0; float x = 0, y = 0, w = 0, h = 0; };
    QVector<MediaQuad> m_mediaQuads;
    QHash<int, QOpenGLTexture *> m_mediaTex;
    QHash<int, quint64> m_mediaRev;
    QVector<Inst> m_deco;
    QVector<Inst> m_overlay;

    // glyph atlas
    QHash<quint64, int> m_glyphIndex;
    QImage m_atlasImage;
    QFont m_atlasFonts[4];
    bool m_texDirty = false;
    QSize m_atlasSize {2048, 2048};
    double m_tileW = 1;
    double m_tileH = 1;
    int m_tileCols = 1;
    int m_tilesUsed = 0;
    int m_atlasFontPx = 0;
    qreal m_atlasDpr = 0;
    bool m_atlasDirty = true;

    // metrics quantised to whole physical pixels, so backgrounds and the grid
    // never land between two pixels
    int m_cellPxW = 8;
    int m_cellPxH = 18;
    int m_orgX = 16;
    int m_orgY = 14;
    double m_gscale = 1.0;
    double m_goffX = 0.0;
    double m_goffY = 0.0;
    double m_thick = 1.5;

    int m_pw = 0;
    int m_ph = 0;
    int m_lastFrameUs = 0;
    int m_debugFrames = 0;
    QColor m_clear;
};

// ---------------------------------------------------------------------------
// GlTermView wrapper
// ---------------------------------------------------------------------------
GlTermView::GlTermView(QWidget *parent)
    : TermSurface(parent)
{
    m_canvas = new Canvas(this);
    m_canvas->setGeometry(rect());
    m_canvas->show();
}

GlTermView::~GlTermView() = default;

QString GlTermView::rendererInfo() const
{
    return m_canvas && m_canvas->ready() ? m_canvas->rendererName()
                                         : QStringLiteral("OpenGL (non pronto)");
}

bool GlTermView::glReady() const
{
    return m_canvas && m_canvas->ready();
}

void GlTermView::setStrokeFont(bool on)
{
    if (m_canvas)
        m_canvas->strokeFont = on;
    m_dirty = true;
    update();
}

bool GlTermView::strokeFont() const
{
    return m_canvas && m_canvas->strokeFont;
}

int GlTermView::lastFrameUs() const
{
    return m_canvas ? m_canvas->lastFrameUs() : 0;
}

QImage GlTermView::grabGlFrame() const
{
    if (!m_canvas || !m_canvas->ready())
        return QImage();
    return m_canvas->grabFramebuffer();
}

void GlTermView::resizeEvent(QResizeEvent *e)
{
    TermSurface::resizeEvent(e);
    if (m_canvas)
        m_canvas->setGeometry(rect());
}

void GlTermView::showEvent(QShowEvent *e)
{
    TermSurface::showEvent(e);
    if (m_canvas)
        m_canvas->setGeometry(rect());
}

void GlTermView::requestPaint()
{
    TermSurface::requestPaint();
    if (m_canvas)
        m_canvas->update();
}

// ---------------------------------------------------------------------------
// Canvas implementation
// ---------------------------------------------------------------------------
void GlTermView::Canvas::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qWarning("neonterm: OpenGL 3.3 core functions unavailable");
        return;
    }

    const GLubyte *r = glGetString(GL_RENDERER);
    const GLubyte *v = glGetString(GL_VERSION);
    m_renderer = QStringLiteral("%1 · %2")
                     .arg(r ? QString::fromLatin1(reinterpret_cast<const char *>(r))
                            : QStringLiteral("?"),
                          v ? QString::fromLatin1(reinterpret_cast<const char *>(v))
                            : QStringLiteral("?"));

    buildPrograms();

    static const float corners[12] = {0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1};

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_cornerVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_cornerVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    glGenBuffers(1, &m_instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(Inst), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Inst),
                          reinterpret_cast<void *>(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Inst),
                          reinterpret_cast<void *>(8 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);

    glGenVertexArrays(1, &m_fsVao);
    glBindVertexArray(m_fsVao);
    glGenBuffers(1, &m_fsVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_ready = true;
    m_atlasDirty = true;
    m_view->m_dirty = true;
}

void GlTermView::Canvas::buildPrograms()
{
    auto make = [](const char *vs, const char *fs) {
        auto *p = new QOpenGLShaderProgram;
        p->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
        p->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
        if (!p->link())
            qWarning("neonterm: shader link failed: %s", qPrintable(p->log()));
        return p;
    };
    m_glyphProg = make(kVsGlyph, kFsGlyph);
    m_quadProg = make(kVsQuad, kFsQuad);
    m_strokeProg = make(kVsStroke, kFsStroke);
    m_mediaProg = make(kVsMedia, kFsMedia);
    m_finalProg = make(kVsFull, kFsFinal);
}

void GlTermView::Canvas::releaseGl()
{
    delete m_atlasTex;
    m_atlasTex = nullptr;
    delete m_scene;
    m_scene = nullptr;
    delete m_glyphProg;
    m_glyphProg = nullptr;
    delete m_quadProg;
    m_quadProg = nullptr;
    delete m_strokeProg;
    m_strokeProg = nullptr;
    delete m_mediaProg;
    m_mediaProg = nullptr;
    for (QOpenGLTexture *t : m_mediaTex)
        delete t;
    m_mediaTex.clear();
    m_mediaRev.clear();
    delete m_finalProg;
    m_finalProg = nullptr;
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_cornerVbo) {
        glDeleteBuffers(1, &m_cornerVbo);
        m_cornerVbo = 0;
    }
    if (m_instVbo) {
        glDeleteBuffers(1, &m_instVbo);
        m_instVbo = 0;
    }
    if (m_fsVao) {
        glDeleteVertexArrays(1, &m_fsVao);
        m_fsVao = 0;
    }
    if (m_fsVbo) {
        glDeleteBuffers(1, &m_fsVbo);
        m_fsVbo = 0;
    }
    m_ready = false;
}

void GlTermView::Canvas::resizeGL(int, int)
{
    // targets are sized in paintGL, where the device pixel ratio is known
}

void GlTermView::Canvas::ensureTargets(int pw, int ph)
{
    if (pw == m_pw && ph == m_ph && m_scene)
        return;
    m_pw = std::max(2, pw);
    m_ph = std::max(2, ph);

    delete m_scene;

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    fmt.setSamples(0);
    m_scene = new QOpenGLFramebufferObject(m_pw, m_ph, fmt);
    m_view->m_dirty = true;
}

bool GlTermView::Canvas::buildAtlas()
{
    const qreal dpr = m_view->devicePixelRatioF();
    const double wantTileW = std::ceil(2.0 * m_view->m_cellW * dpr) + 2.0;
    const double wantTileH = std::ceil(m_view->m_cellH * dpr) + 2.0;

    if (m_atlasTex && m_atlasFontPx == m_view->m_fontPx
        && std::abs(m_atlasDpr - dpr) < 0.001 && std::abs(m_tileW - wantTileW) < 0.6
        && std::abs(m_tileH - wantTileH) < 0.6) {
        m_atlasDirty = false;
        return false;
    }

    delete m_atlasTex;
    m_atlasTex = nullptr;
    m_glyphIndex.clear();
    m_tilesUsed = 0;
    m_atlasFontPx = m_view->m_fontPx;
    m_atlasDpr = dpr;
    m_tileW = wantTileW;
    m_tileH = wantTileH;
    m_tileCols = std::max(1, int(m_atlasSize.width() / m_tileW));

    m_atlasImage = QImage(m_atlasSize, QImage::Format_ARGB32_Premultiplied);
    m_atlasImage.fill(Qt::transparent);

    // rasterised at physical resolution: glyphs stay sharp at 1.4x, and the
    // shader only has to scale by the fractional part
    for (int i = 0; i < 4; ++i) {
        QFont f = theme::mono(int(std::lround(m_view->m_fontPx * dpr)));
        if (i & 1)
            f.setWeight(QFont::DemiBold);
        f.setItalic(i & 2);
        f.setHintingPreference(QFont::PreferFullHinting);
        m_atlasFonts[i] = f;
    }

    m_atlasDirty = false;
    m_texDirty = false;
    m_view->m_dirty = true;
    return true;
}

void GlTermView::Canvas::uploadAtlas()
{
    if (!m_texDirty)
        return;
    const QImage a8 = m_atlasImage.convertToFormat(QImage::Format_Alpha8);
    if (!m_atlasTex) {
        m_atlasTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
        m_atlasTex->setFormat(QOpenGLTexture::R8_UNorm);
        m_atlasTex->setSize(m_atlasSize.width(), m_atlasSize.height());
        m_atlasTex->setMipLevels(1);
        m_atlasTex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
        m_atlasTex->setMinificationFilter(QOpenGLTexture::Linear);
        m_atlasTex->setMagnificationFilter(QOpenGLTexture::Linear);
        m_atlasTex->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    QOpenGLPixelTransferOptions opts;
    opts.setAlignment(1);
    m_atlasTex->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, a8.constBits(), &opts);
    m_texDirty = false;
}

int GlTermView::Canvas::glyphFor(char32_t cp, int style, int widthCells)
{
    if (m_atlasDirty)
        return 0;
    const quint64 key = (quint64(cp) << 8) | quint64(style & 0xf) | (quint64(widthCells) << 4);
    const auto it = m_glyphIndex.constFind(key);
    if (it != m_glyphIndex.constEnd())
        return *it;

    const int maxTiles = m_tileCols * int(m_atlasSize.height() / m_tileH);
    if (m_tilesUsed >= maxTiles)
        return 0;

    const int idx = m_tilesUsed++;
    const int gx = idx % m_tileCols;
    const int gy = idx / m_tileCols;

    QPainter p(&m_atlasImage);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(m_atlasFonts[style & 3]);
    p.setPen(Qt::white);
    const QFontMetricsF fm(m_atlasFonts[style & 3]);
    const double baseline = gy * m_tileH + 1.0 + fm.ascent();
    p.drawText(QPointF(gx * m_tileW + 1.0, baseline), QString::fromUcs4(&cp, 1));
    p.end();

    m_texDirty = true;
    m_glyphIndex.insert(key, idx);
    return idx;
}

void GlTermView::Canvas::addQuad(QVector<Inst> &out, double x, double y, double w, double h,
                                 const QColor &col)
{
    Inst in;
    in.a = float(x);
    in.b = float(y);
    in.c = float(w);
    in.d = float(h);
    in.r = float(col.redF());
    in.g = float(col.greenF());
    in.bl = float(col.blueF());
    in.al = float(col.alphaF());
    out.push_back(in);
}

void GlTermView::Canvas::buildFrame()
{
    m_bg.clear();
    m_glyphs.clear();
    m_strokes.clear();
    m_deco.clear();
    m_overlay.clear();

    const TermSurface::PaintState st = m_view->paintState();
    if (!st.session || !st.palette)
        return;

    const Screen &scr = st.session->screen();
    const Palette &pal = *st.palette;
    const int hist = scr.historySize();
    const int topAbs = hist - st.scrollOffset;
    const double dpr = m_view->devicePixelRatioF();

    // The grid is snapped to whole physical pixels: background tiles, the
    // selection and the cursor can then never land between two pixels.
    m_cellPxW = std::max(4, int(std::lround(st.cellW * dpr)));
    m_cellPxH = std::max(6, int(std::lround(st.cellH * dpr)));
    m_orgX = int(std::lround(st.padX * dpr));
    m_orgY = int(std::lround(st.padY * dpr));
    m_gscale = std::min(double(m_cellPxW) / neonfont::kBoxW,
                        double(m_cellPxH) / neonfont::kBoxH);
    m_goffX = (double(m_cellPxW) - neonfont::kBoxW * m_gscale) * 0.5;
    m_goffY = (double(m_cellPxH) - neonfont::kBoxH * m_gscale) * 0.5;
    m_thick = std::max(1.2, double(m_cellPxH) * 0.095);

    const QColor defFg = toColor(pal.fg);
    const QColor defBg = toColor(pal.bg);
    const QColor accent = theme::look().accent;
    const double pxY = 1.0 / double(m_cellPxH);   // cells per physical pixel
    const double pxX = 1.0 / double(m_cellPxW);

    auto resolveFg = [&](const Cell &c) {
        QRgb fg = c.fg ? c.fg : pal.fg;
        if (c.flags & CF_Reverse)
            fg = c.bg ? c.bg : pal.bg;
        if (!(c.flags & CF_Reverse) && c.fg == 0 && (c.flags & CF_Bold))
            fg = QColor::fromRgb(fg).lighter(116).rgb();
        if (c.flags & CF_Dim)
            fg = QColor::fromRgb(fg).darker(145).rgb();
        return toColor(fg);
    };

    for (int r = 0; r < st.rows; ++r) {
        const int abs = topAbs + r;
        if (abs < 0 || abs >= hist + scr.rows())
            continue;
        const std::vector<Cell> &cells = scr.absLine(abs);
        const int limit = std::min<int>(st.cols, int(cells.size()));

        // --- background runs
        int c = 0;
        while (c < limit) {
            const Cell &cell = cells[size_t(c)];
            const bool rev = (cell.flags & CF_Reverse) != 0;
            const QRgb bg = cell.bg;
            if (bg == 0 && !rev) {   // plain default background: nothing to paint
                ++c;
                continue;
            }
            const QColor col = rev ? (cell.fg ? toColor(cell.fg) : defFg) : toColor(bg);
            const int start = c;
            while (c < limit) {
                const Cell &k = cells[size_t(c)];
                if (k.bg != bg || ((k.flags & CF_Reverse) != 0) != rev)
                    break;
                ++c;
            }
            addQuad(m_bg, start, r, c - start, 1.0, col);
        }

        // --- glyphs: our own stroke font first, the atlas only for what it
        //     does not cover (CJK, emoji, other scripts)
        c = 0;
        while (c < limit) {
            const Cell &cell = cells[size_t(c)];
            if (cell.flags & CF_Cont) {
                ++c;
                continue;
            }
            const int wc = widthCellsOf(cells, c, limit);
            if (cell.blank() && wc == 1) {
                ++c;
                continue;
            }
            const bool bold = (cell.flags & CF_Bold) != 0;
            const QColor fg = resolveFg(cell);

            QVector<neonfont::Rect> rects;
            const QVector<neonfont::Segment> *segs = strokeFont ? neonfont::glyph(cell.ch)
                                                                : nullptr;
            if (segs && !segs->isEmpty()) {
                const float t = float(bold ? m_thick * 1.55 : m_thick);
                for (const neonfont::Segment &sg : *segs) {
                    Inst in;
                    in.a = sg.x0;
                    in.b = sg.y0;
                    in.c = sg.x1;
                    in.d = sg.y1;
                    in.r = float(fg.redF());
                    in.g = float(fg.greenF());
                    in.bl = float(fg.blueF());
                    in.al = float(fg.alphaF());
                    in.e = t;
                    in.f = float(c);
                    in.g2 = float(r);
                    m_strokes.push_back(in);
                }
            } else if (neonfont::boxRects(cell.ch, rects)) {
                int alpha = 255;
                if (cell.ch == 0x2591)
                    alpha = 55;
                else if (cell.ch == 0x2592)
                    alpha = 115;
                else if (cell.ch == 0x2593)
                    alpha = 185;
                QColor bc = fg;
                bc.setAlpha(alpha);
                for (const neonfont::Rect &rc : rects)
                    addQuad(m_deco, c + rc.x, r + rc.y, rc.w, rc.h, bc);
            } else {
                const int style = (bold ? 1 : 0) | (cell.flags & CF_Italic ? 2 : 0);
                const int gi = glyphFor(cell.ch, style, wc);
                Inst in;
                in.a = float(c);
                in.b = float(r);
                in.c = float(gi);
                in.d = float(wc);
                in.r = float(fg.redF());
                in.g = float(fg.greenF());
                in.bl = float(fg.blueF());
                in.al = 1.0f;
                m_glyphs.push_back(in);
            }

            if (cell.flags & CF_Underline)
                addQuad(m_deco, c, r + 1.0 - 2.0 * pxY, wc, 1.6 * pxY, fg);
            if (cell.flags & CF_Strike)
                addQuad(m_deco, c, r + 0.5, wc, 1.4 * pxY, fg);
            c += wc;
        }
    }

    // --- media geometry (the textures are refreshed at draw time)
    m_mediaQuads.clear();
    if (st.media && st.store) {
        for (const MediaItem &it : *st.media) {
            if (it.abs + it.rows <= topAbs || it.abs >= topAbs + st.rows)
                continue;
            QSize src = st.store->sourceSize(it);
            if (src.width() <= 0 || src.height() <= 0) {
                QImage img = st.store->frame(it);
                src = img.size();
            }
            if (src.width() <= 0 || src.height() <= 0)
                continue;
            const double cw = it.cols * st.cellW;
            const double ch = it.rows * st.cellH;
            const double sc = std::min(cw / src.width(), ch / src.height());
            const double w = src.width() * sc;
            const double h = src.height() * sc;
            MediaQuad q;
            q.id = it.id;
            q.w = float(w / st.cellW);
            q.h = float(h / st.cellH);
            q.x = float(it.col + (cw - w) * 0.5 / st.cellW);
            q.y = float((it.abs - topAbs) + (ch - h) * 0.5 / st.cellH);
            m_mediaQuads.push_back(q);
        }
    }

    // --- selection (under the glyphs)
    if (st.hasSelection) {
        int aAbs = st.selAabs, aCol = st.selAcol;
        int bAbs = st.selBabs, bCol = st.selBcol;
        if (aAbs > bAbs || (aAbs == bAbs && aCol > bCol)) {
            std::swap(aAbs, bAbs);
            std::swap(aCol, bCol);
        }
        QColor sel = accent;
        sel.setAlpha(58);
        for (int abs = std::max(aAbs, topAbs); abs <= std::min(bAbs, topAbs + st.rows - 1); ++abs) {
            const int r = abs - topAbs;
            const int c0 = (abs == aAbs) ? aCol : 0;
            const int c1 = (abs == bAbs) ? bCol : st.cols - 1;
            if (c1 < c0)
                continue;
            addQuad(m_overlay, c0, r + 0.02, c1 - c0 + 1, 0.96, sel);
        }
    }

    // --- cursor (over the glyphs, like every terminal)
    if (scr.cursorVisible() && st.scrollOffset == 0) {
        const Pos cur = scr.cursor();
        if (cur.y >= 0 && cur.y < st.rows && cur.x >= 0 && cur.x < st.cols) {
            QColor c = accent;
            const int style = scr.cursorStyle();
            if (style == 1) {
                c.setAlpha(235);
                addQuad(m_deco, cur.x, cur.y + 1.0 - 3.0 * pxY, 1.0, 3.0 * pxY, c);
            } else if (style == 2) {
                c.setAlpha(235);
                addQuad(m_deco, cur.x, cur.y, 2.0 * pxX, 1.0, c);
            } else {
                QColor fill = c;
                fill.setAlpha(m_view->hasFocus() ? (st.cursorOn ? 58 : 22) : 16);
                addQuad(m_deco, cur.x, cur.y + 1.0 * pxY, 1.0, 1.0 - 2.0 * pxY, fill);
                QColor edge = c;
                edge.setAlpha(m_view->hasFocus() ? (st.cursorOn ? 235 : 90) : 70);
                const double t = 1.4 * pxY;
                addQuad(m_deco, cur.x, cur.y, 1.0, t, edge);
                addQuad(m_deco, cur.x, cur.y + 1.0 - t, 1.0, t, edge);
                addQuad(m_deco, cur.x, cur.y, 1.4 * pxX, 1.0, edge);
                addQuad(m_deco, cur.x + 1.0 - 1.4 * pxX, cur.y, 1.4 * pxX, 1.0, edge);
            }
        }
    }

    // --- scrollbar
    if (hist > 0) {
        const double trackH = st.rows;
        QColor track(0x10, 0x1c, 0x26);
        addQuad(m_deco, st.cols + 0.15, 0, 0.22, trackH, track);
        const double total = double(hist + st.rows);
        const double thumbH = std::max(1.0, trackH * st.rows / total);
        const double progress = hist > 0 ? double(hist - st.scrollOffset) / double(hist) : 1.0;
        QColor thumb = accent;
        thumb.setAlpha(150);
        addQuad(m_deco, st.cols + 0.15, (trackH - thumbH) * progress, 0.22, thumbH, thumb);
    }

    m_clear = defBg;
}

void GlTermView::Canvas::drawMedia(const TermSurface::PaintState &st)
{
    if (m_mediaQuads.isEmpty() || !st.store || !m_mediaProg)
        return;

    // the overlay geometry comes from the surface, so a click and a pixel agree
    const QVector<TermSurface::MediaUi> uis = m_view->mediaUi();

    m_mediaProg->bind();
    m_mediaProg->setUniformValue("uCell", QVector2D(float(m_cellPxW), float(m_cellPxH)));
    m_mediaProg->setUniformValue("uOrigin", QVector2D(float(m_orgX), float(m_orgY)));
    m_mediaProg->setUniformValue("uViewport", QVector2D(float(m_pw), float(m_ph)));
    m_mediaProg->setUniformValue("uTex", 0);

    auto drawImage = [this](int texKey, const QImage &src, QVector4D rect) {
        if (src.isNull())
            return;
        const QImage rgba = src.convertToFormat(QImage::Format_RGBA8888);
        QOpenGLTexture *tex = m_mediaTex.value(texKey);
        if (!tex || tex->width() != rgba.width() || tex->height() != rgba.height()) {
            delete tex;
            tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
            tex->setFormat(QOpenGLTexture::RGBA8_UNorm);
            tex->setSize(rgba.width(), rgba.height());
            tex->setMipLevels(1);
            tex->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            tex->setMinificationFilter(QOpenGLTexture::Linear);
            tex->setMagnificationFilter(QOpenGLTexture::Linear);
            tex->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_mediaTex.insert(texKey, tex);
            m_mediaRev.insert(texKey, 0);
        }
        const quint64 key = src.cacheKey();
        if (m_mediaRev.value(texKey) != key) {
            QOpenGLPixelTransferOptions opts;
            opts.setAlignment(4);
            tex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, rgba.constBits(), &opts);
            m_mediaRev.insert(texKey, key);
        }
        m_mediaProg->setUniformValue("uRect", rect);
        glActiveTexture(GL_TEXTURE0);
        tex->bind();
        glBindVertexArray(m_fsVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    };

    for (const MediaQuad &q : m_mediaQuads) {
        const MediaItem *item = nullptr;
        if (st.media) {
            for (const MediaItem &m : *st.media) {
                if (m.id == q.id) {
                    item = &m;
                    break;
                }
            }
        }
        if (!item)
            continue;
        const QImage img = st.store->frame(*item);
        if (qEnvironmentVariableIsSet("NEONTERM_DEBUG_MEDIA"))
            qInfo("gl quad %d: %.1f,%.1f %.1fx%.1f cells (cell %.2fx%.2f)",
                  q.id, q.x, q.y, q.w, q.h, st.cellW, st.cellH);
        drawImage(q.id, img, QVector4D(q.x, q.y, q.w, q.h));

        // controls: the bar sits on the bottom edge of the image, the close
        // button in the top right corner
        for (const TermSurface::MediaUi &ui : uis) {
            if (ui.id != q.id)
                continue;
            auto toCells = [&st](const QRectF &r) {
                return QVector4D(float((r.x() - st.padX) / st.cellW),
                                 float((r.y() - st.padY) / st.cellH),
                                 float(r.width() / st.cellW), float(r.height() / st.cellH));
            };
            const QImage bar = m_view->mediaBarImage(ui);
            if (!bar.isNull())
                drawImage(q.id * 4 + 1, bar, toCells(ui.bar));
            const QImage close = m_view->mediaCloseImage(ui);
            if (!close.isNull())
                drawImage(q.id * 4 + 2, close, toCells(ui.close));
        }
    }
    m_mediaProg->release();
}

void GlTermView::Canvas::drawStrokes()
{
    if (m_strokes.isEmpty())
        return;
    m_strokeProg->bind();
    m_strokeProg->setUniformValue("uCell", QVector2D(float(m_cellPxW), float(m_cellPxH)));
    m_strokeProg->setUniformValue("uOrigin", QVector2D(float(m_orgX), float(m_orgY)));
    m_strokeProg->setUniformValue("uViewport", QVector2D(float(m_pw), float(m_ph)));
    m_strokeProg->setUniformValue("uScale", float(m_gscale));
    m_strokeProg->setUniformValue("uGlyphOff", QVector2D(float(m_goffX), float(m_goffY)));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glBufferData(GL_ARRAY_BUFFER, int(m_strokes.size() * sizeof(Inst)), m_strokes.constData(),
                 GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, int(m_strokes.size()));
    glBindVertexArray(0);
    m_strokeProg->release();
}

void GlTermView::Canvas::uploadAndDraw(QOpenGLShaderProgram *prog, const QVector<Inst> &inst,
                                       int count, bool textured)
{
    if (count <= 0)
        return;
    prog->bind();
    prog->setUniformValue("uCell", QVector2D(float(m_cellPxW), float(m_cellPxH)));
    prog->setUniformValue("uOrigin", QVector2D(float(m_orgX), float(m_orgY)));
    prog->setUniformValue("uViewport", QVector2D(float(m_pw), float(m_ph)));
    prog->setUniformValue("uTiles",
                          QVector2D(float(m_tileCols),
                                    float(std::max(1, int(m_atlasSize.height() / m_tileH)))));
    prog->setUniformValue("uTileW", float(m_tileW));
    prog->setUniformValue("uTileH", float(m_tileH));
    prog->setUniformValue("uAtlasSize",
                          QVector2D(float(m_atlasSize.width()), float(m_atlasSize.height())));
    if (textured && m_atlasTex) {
        glActiveTexture(GL_TEXTURE0);
        m_atlasTex->bind();
        prog->setUniformValue("uAtlas", 0);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glBufferData(GL_ARRAY_BUFFER, int(inst.size() * sizeof(Inst)), inst.constData(),
                 GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    glBindVertexArray(0);
    prog->release();
}

void GlTermView::Canvas::drawFullscreen(QOpenGLShaderProgram *prog, GLuint tex, GLuint tex2)
{
    glBindVertexArray(m_fsVao);
    prog->bind();
    if (tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        prog->setUniformValue("uScene", 0);
        prog->setUniformValue("uSrc", 0);
    }
    if (tex2) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex2);
        prog->setUniformValue("uBloom", 1);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    prog->release();
    glBindVertexArray(0);
}

void GlTermView::Canvas::paintGL()
{
    m_view->countFrame();
    if (!m_ready)
        return;

    QElapsedTimer timer;
    timer.start();

    const qreal dpr = m_view->devicePixelRatioF();
    ensureTargets(int(std::lround(width() * dpr)), int(std::lround(height() * dpr)));
    const bool atlasRebuilt = buildAtlas();
    if (m_view->needsRepaint() || atlasRebuilt)
        buildFrame();
    uploadAtlas();

    // ---- scene
    m_scene->bind();
    glViewport(0, 0, m_pw, m_ph);
    glClearColor(float(m_clear.redF()), float(m_clear.greenF()), float(m_clear.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    uploadAndDraw(m_quadProg, m_bg, int(m_bg.size()), false);
    uploadAndDraw(m_quadProg, m_overlay, int(m_overlay.size()), false);
    drawMedia(m_view->paintState());
    uploadAndDraw(m_glyphProg, m_glyphs, int(m_glyphs.size()), true);
    drawStrokes();
    uploadAndDraw(m_quadProg, m_deco, int(m_deco.size()), false);
    m_scene->release();

    const GLuint sceneTex = m_scene->texture();
    const GLuint defaultFbo = GLuint(defaultFramebufferObject());

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    glViewport(0, 0, m_pw, m_ph);
    glDisable(GL_BLEND);
    m_finalProg->bind();
    m_finalProg->setUniformValue("uCrt", m_view->m_crt ? 1.0f : 0.0f);
    m_finalProg->setUniformValue("uTexel",
                                 QVector2D(1.0f / float(m_pw), 1.0f / float(m_ph)));
    drawFullscreen(m_finalProg, sceneTex, 0);
    m_finalProg->release();

    m_view->markPainted();
    m_lastFrameUs = int(timer.nsecsElapsed() / 1000);
    if (!qEnvironmentVariableIsEmpty("NEONTERM_DEBUG_GL") && (++m_debugFrames % 10) == 0)
        qInfo("neonterm gl: %d us/frame, %d glyph segments, %d atlas glyphs", m_lastFrameUs,
              int(m_strokes.size()), int(m_glyphs.size()));
}
