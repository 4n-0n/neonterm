#pragma once

#include <QByteArray>

// demoScript: a self-contained ANSI screen used by --demo and by the tests.
// It exercises truecolor, 256-color, SGR attributes, reverse video and
// double-width cells that are not emoji.
QByteArray demoScript();
