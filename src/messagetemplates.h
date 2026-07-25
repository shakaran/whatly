#ifndef MESSAGETEMPLATES_H
#define MESSAGETEMPLATES_H

#include <QList>
#include <QString>

// Whatly's own reusable message templates: a named body with {{placeholder}}
// fields, filled at send time (Messaging::fillTemplate) and used by
// `whatly --send --template <name> --var key=value`. Stored per account, so
// each account keeps its own set. The filling/placeholder logic lives in the
// Messaging core; this is just the named store.
namespace MessageTemplates {

struct Template {
  QString name;
  QString body;
};

// All templates, in stored order (skips any with an empty name).
QList<Template> all();
void setAll(const QList<Template> &templates);

// Whether a template with this name exists (case-sensitive).
bool exists(const QString &name);

// The body for `name`, or an empty string if there is no such template.
QString body(const QString &name);

// Add a template or replace the body of an existing one with the same name.
void set(const QString &name, const QString &body);

// Remove the template named `name`; returns true if one was removed.
bool remove(const QString &name);

} // namespace MessageTemplates

#endif // MESSAGETEMPLATES_H
