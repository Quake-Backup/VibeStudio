#pragma once

#include "core/language_hover.h"
class QObject;
class QTextDocument;

namespace slopstudio {

// Shared rendering for provider documentation. No file/network resources,
// raw HTML, active links or images; selectable literal text remains available.
QTextDocument* createLanguageDocumentationDocument(QObject* parent = nullptr);
void renderLanguageDocumentation(QTextDocument* document, const QVector<LanguageHoverPart>& parts);

} // namespace slopstudio
