#ifndef CUSTOMCSS_H
#define CUSTOMCSS_H

#include <QString>

class QWebEngineProfile;

// User-provided CSS injected into WhatsApp Web, for the many community
// stylesheets that exist for it (catppuccin and the like). Orthogonal to the
// built-in chat themes: this is raw CSS the user supplies, applied on top.
//
// The CSS is stored as a file inside the app's data directory and injected as a
// <style> in a separate MainWorld userscript, so it survives WhatsApp rebuilding
// its DOM and needs no file access from the page.
namespace CustomCss {

// Whether custom CSS is currently enabled and non-empty.
bool isActive();

// The stored CSS, or empty when none is set.
QString css();

// Replace the stored CSS with the contents of a file. Returns false and fills
// `error` if the file cannot be read.
bool setFromFile(const QString &path, QString *error);

void clear();

// The injecting userscript for the current CSS, empty when inactive. Also used
// to apply a change to an already-loaded page.
QString scriptSource();

void install(QWebEngineProfile *profile);

} // namespace CustomCss

#endif // CUSTOMCSS_H
