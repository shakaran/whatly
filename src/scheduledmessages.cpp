#include "scheduledmessages.h"
#include "appprofile.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

// How often the timer wakes to look for due messages.
constexpr int kPollMs = 30 * 1000;
// A send that has not reported back within this long is treated as failed so
// the queue can move on.
constexpr qint64 kSendTimeoutMs = 120 * 1000;

QString digitsOnly(const QString &number) {
  QString out;
  for (const QChar c : number)
    if (c.isDigit())
      out += c;
  return out;
}

QString newId() {
  // Enough to be unique in a personal schedule; not security-sensitive.
  return QString::number(QDateTime::currentMSecsSinceEpoch(), 36) +
         QString::number(QRandomGenerator::global()->generate(), 36);
}

QString statusToString(ScheduledMessages::Status s) {
  switch (s) {
  case ScheduledMessages::Status::Sent:
    return QStringLiteral("sent");
  case ScheduledMessages::Status::Failed:
    return QStringLiteral("failed");
  case ScheduledMessages::Status::Pending:
    break;
  }
  return QStringLiteral("pending");
}

ScheduledMessages::Status statusFromString(const QString &s) {
  if (s == QLatin1String("sent"))
    return ScheduledMessages::Status::Sent;
  if (s == QLatin1String("failed"))
    return ScheduledMessages::Status::Failed;
  return ScheduledMessages::Status::Pending;
}

QString recurrenceToString(ScheduledMessages::Recurrence r) {
  switch (r) {
  case ScheduledMessages::Recurrence::Daily:
    return QStringLiteral("daily");
  case ScheduledMessages::Recurrence::Weekdays:
    return QStringLiteral("weekdays");
  case ScheduledMessages::Recurrence::Weekly:
    return QStringLiteral("weekly");
  case ScheduledMessages::Recurrence::None:
    break;
  }
  return QStringLiteral("none");
}

ScheduledMessages::Recurrence recurrenceFromString(const QString &s) {
  if (s == QLatin1String("daily"))
    return ScheduledMessages::Recurrence::Daily;
  if (s == QLatin1String("weekdays"))
    return ScheduledMessages::Recurrence::Weekdays;
  if (s == QLatin1String("weekly"))
    return ScheduledMessages::Recurrence::Weekly;
  return ScheduledMessages::Recurrence::None;
}

// A JS double-quoted literal.
QString jsString(const QString &value) {
  QString e = value;
  e.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  e.replace(QLatin1Char('"'), QLatin1String("\\\""));
  e.replace(QLatin1Char('\n'), QLatin1String("\\n"));
  e.replace(QLatin1Char('\r'), QLatin1String("\\r"));
  return QLatin1Char('"') + e + QLatin1Char('"');
}

} // namespace

ScheduledMessages::ScheduledMessages(QObject *parent) : QObject(parent) {
  load();
}

QString ScheduledMessages::storagePath() const {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/whatly-scheduled") + AppProfile::suffix() +
         QStringLiteral(".json");
}

void ScheduledMessages::load() {
  QFile f(storagePath());
  if (!f.open(QIODevice::ReadOnly))
    return;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  m_entries.clear();
  for (const QJsonValue &v : doc.array()) {
    const QJsonObject o = v.toObject();
    Entry e;
    e.id = o.value("id").toString();
    e.number = o.value("number").toString();
    e.name = o.value("name").toString();
    e.text = o.value("text").toString();
    e.dueAt = QDateTime::fromMSecsSinceEpoch(
        qint64(o.value("dueAt").toDouble()));
    e.createdAt = QDateTime::fromMSecsSinceEpoch(
        qint64(o.value("createdAt").toDouble()));
    e.status = statusFromString(o.value("status").toString());
    e.error = o.value("error").toString();
    e.recurrence = recurrenceFromString(o.value("recurrence").toString());
    e.reminder = o.value("reminder").toBool(false);
    if (!e.id.isEmpty())
      m_entries.append(e);
  }
}

void ScheduledMessages::save() const {
  QJsonArray arr;
  for (const Entry &e : m_entries) {
    QJsonObject o;
    o["id"] = e.id;
    o["number"] = e.number;
    o["name"] = e.name;
    o["text"] = e.text;
    o["dueAt"] = double(e.dueAt.toMSecsSinceEpoch());
    o["createdAt"] = double(e.createdAt.toMSecsSinceEpoch());
    o["status"] = statusToString(e.status);
    o["error"] = e.error;
    o["recurrence"] = recurrenceToString(e.recurrence);
    o["reminder"] = e.reminder;
    arr.append(o);
  }
  QSaveFile f(storagePath());
  if (!f.open(QIODevice::WriteOnly))
    return;
  f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
  f.commit();
}

ScheduledMessages::Entry *ScheduledMessages::find(const QString &id) {
  for (Entry &e : m_entries)
    if (e.id == id)
      return &e;
  return nullptr;
}

QString ScheduledMessages::add(const QString &number, const QString &name,
                               const QString &text, const QDateTime &dueAt,
                               Recurrence recurrence, bool reminder) {
  Entry e;
  e.id = newId();
  e.number = digitsOnly(number);
  e.name = name.trimmed();
  e.text = text;
  e.dueAt = dueAt;
  e.createdAt = QDateTime::currentDateTime();
  e.status = Status::Pending;
  e.recurrence = recurrence;
  e.reminder = reminder;
  m_entries.append(e);
  save();
  emit changed();
  // A message scheduled for a moment already past should go out now.
  checkDue();
  return e.id;
}

void ScheduledMessages::remove(const QString &id) {
  if (m_sendingId == id)
    m_sendingId.clear(); // its result no longer matters
  for (int i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].id == id) {
      m_entries.removeAt(i);
      save();
      emit changed();
      return;
    }
  }
}

void ScheduledMessages::removeCompleted() {
  const int before = m_entries.size();
  for (int i = m_entries.size() - 1; i >= 0; --i)
    if (m_entries[i].status != Status::Pending)
      m_entries.removeAt(i);
  if (m_entries.size() != before) {
    save();
    emit changed();
  }
}

void ScheduledMessages::reportResult(const QString &id, bool ok,
                                     const QString &error) {
  if (Entry *e = find(id)) {
    if (ok) {
      // A recurring message reschedules itself; a one-shot is marked Sent.
      advanceOrComplete(*e);
    } else {
      e->status = Status::Failed;
      e->error = error;
    }
    save();
    emit changed();
  }
  if (m_sendingId == id) {
    m_sendingId.clear();
    // Immediately look for the next due message rather than waiting a tick.
    checkDue();
  }
}

void ScheduledMessages::start() {
  if (!m_started) {
    m_started = true;
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &ScheduledMessages::checkDue);
    m_timer->start();
  }
  checkDue();
}

void ScheduledMessages::checkDue() {
  // Something is already in flight: wait for it, unless it has wedged.
  if (!m_sendingId.isEmpty()) {
    if (m_sendingSince.isValid() &&
        m_sendingSince.msecsTo(QDateTime::currentDateTime()) > kSendTimeoutMs) {
      const QString stuck = m_sendingId;
      m_sendingId.clear();
      reportResult(stuck, false,
                   tr("Timed out waiting for the message to send"));
    }
    return;
  }
  if (!m_started)
    return; // do not send until WhatsApp Web is up (start() gates this)

  const QDateTime now = QDateTime::currentDateTime();

  // Reminders never touch the page, so fire every overdue one immediately
  // (advancing recurrence) without occupying the single send slot.
  bool firedReminder = false;
  for (Entry &e : m_entries) {
    if (e.reminder && e.status == Status::Pending && e.dueAt <= now) {
      emit reminderDue(e.id, e.name, e.text);
      advanceOrComplete(e);
      firedReminder = true;
    }
  }
  if (firedReminder) {
    save();
    emit changed();
  }

  Entry *due = nullptr;
  for (Entry &e : m_entries) {
    if (e.reminder || e.status != Status::Pending || e.dueAt > now)
      continue;
    if (!due || e.dueAt < due->dueAt)
      due = &e;
  }
  if (!due)
    return;

  m_sendingId = due->id;
  m_sendingSince = now;
  emit sendRequested(due->id, due->number, due->text);
}

// After a successful fire: a recurring entry is rescheduled to its next
// occurrence and stays Pending; a one-shot is marked Sent.
void ScheduledMessages::advanceOrComplete(Entry &e) {
  if (e.recurrence != Recurrence::None) {
    const QDateTime next = nextOccurrence(e.dueAt, e.recurrence);
    if (next.isValid()) {
      e.dueAt = next;
      e.status = Status::Pending;
      e.error.clear();
      return;
    }
  }
  e.status = Status::Sent;
}

QDateTime ScheduledMessages::nextOccurrence(const QDateTime &from,
                                            Recurrence recurrence) {
  if (recurrence == Recurrence::None || !from.isValid())
    return QDateTime();
  QDateTime next = from;
  switch (recurrence) {
  case Recurrence::Daily:
    return next.addDays(1);
  case Recurrence::Weekly:
    return next.addDays(7);
  case Recurrence::Weekdays: {
    // Skip to the next Mon–Fri day at the same time.
    do {
      next = next.addDays(1);
    } while (next.date().dayOfWeek() > 5); // 6 = Sat, 7 = Sun
    return next;
  }
  case Recurrence::None:
    break;
  }
  return QDateTime();
}

QString ScheduledMessages::recurrenceLabel(Recurrence recurrence) {
  switch (recurrence) {
  case Recurrence::Daily:
    return tr("Daily");
  case Recurrence::Weekdays:
    return tr("Weekdays");
  case Recurrence::Weekly:
    return tr("Weekly");
  case Recurrence::None:
    break;
  }
  return tr("Once");
}

QString ScheduledMessages::statusLabel(Status status) {
  switch (status) {
  case Status::Sent:
    return tr("Sent");
  case Status::Failed:
    return tr("Failed");
  case Status::Pending:
    break;
  }
  return tr("Pending");
}

// ── The page automation ─────────────────────────────────────────────────────
//
// senderScriptSource() is injected into every WhatsApp Web load. It looks for a
// job left in sessionStorage (which survives the navigation the starter causes)
// and, once the chat has opened, clicks Send. It reports the outcome over the
// existing PageBridge (window.__whatlyBridge.scheduledMessageResult).
QString ScheduledMessages::senderScriptSource() {
  return QString::fromLatin1(R"JS(
(function () {
  'use strict';
  if (window.__whatlyScheduledReady) return;
  window.__whatlyScheduledReady = true;
  var KEY = 'whatlyScheduledJob';

  function report(id, ok, err) {
    try {
      if (window.__whatlyBridge && window.__whatlyBridge.scheduledMessageResult)
        window.__whatlyBridge.scheduledMessageResult(String(id), !!ok, String(err || ''));
    } catch (e) { /* bridge not ready yet; the C++ side times out */ }
  }
  function composer() {
    var b = document.querySelectorAll(
      'footer div[contenteditable="true"][role="textbox"],'
      + 'div[contenteditable="true"][data-tab]');
    return b.length ? b[b.length - 1] : null;
  }
  function sendButton() {
    // 'wds-ic-send-filled' on current WhatsApp Web; 'send' was the older name
    // and is no longer on the page at all. The two aria-labels are a last
    // resort and only cover English and Spanish, so without the current icon
    // name this found nothing and the job failed on its own deadline with
    // "timeout waiting for the chat to open".
    var icon = document.querySelector('footer [data-icon="wds-ic-send-filled"]')
      || document.querySelector('[data-icon="wds-ic-send-filled"]')
      || document.querySelector('footer span[data-icon="send"]')
      || document.querySelector('span[data-icon="send"]')
      || document.querySelector('button[aria-label="Send"],button[aria-label="Enviar"]');
    if (!icon) return null;
    return icon.closest('button,[role="button"]') || icon;
  }

  function process() {
    var raw;
    try { raw = sessionStorage.getItem(KEY); } catch (e) { return; }
    if (!raw) return;
    var job;
    try { job = JSON.parse(raw); } catch (e) {
      try { sessionStorage.removeItem(KEY); } catch (e2) {}
      return;
    }
    var deadline = job.deadline || (Date.now() + 45000);
    job.deadline = deadline;
    try { sessionStorage.setItem(KEY, JSON.stringify(job)); } catch (e) {}

    var done = false;
    var finish = function (ok, err) {
      if (done) return;
      done = true;
      clearInterval(timer);
      try { sessionStorage.removeItem(KEY); } catch (e) {}
      report(job.id, ok, err);
    };

    var timer = setInterval(function () {
      try {
        if (Date.now() > deadline) {
          finish(false, 'timeout waiting for the chat to open');
          return;
        }
        var box = composer();
        var btn = sendButton();
        // The deep link prefills the text, so once the chat is open the Send
        // button is present. If for some reason the box is empty, type it.
        if (box && (!box.textContent || box.textContent.trim() === '') && job.text) {
          box.focus();
          try { document.execCommand('insertText', false, job.text); } catch (e) {}
        }
        if (btn) {
          btn.click();
          // Confirm it actually sent: the composer should clear shortly after.
          setTimeout(function () {
            var b = composer();
            finish(true, '');
          }, 400);
          return;
        }
        // No send button and no composer means the number was likely invalid,
        // or the account is not logged in; keep trying until the deadline.
      } catch (e) { /* keep trying until the deadline */ }
    }, 700);
  }

  if (document.readyState === 'complete') setTimeout(process, 1500);
  else window.addEventListener('load', function () { setTimeout(process, 1500); });
})();
)JS");
}

// startJobScript() records the job (so it survives the reload) and opens the
// recipient's chat with the text prefilled via WhatsApp's click-to-chat URL.
QString ScheduledMessages::startJobScript(const QString &id,
                                          const QString &number,
                                          const QString &text) {
  const QString url = QStringLiteral("https://web.whatsapp.com/send?phone=%1&text=%2")
                          .arg(digitsOnly(number),
                               QString::fromLatin1(QUrl::toPercentEncoding(text)));
  return QStringLiteral(R"JS(
(function () {
  try {
    sessionStorage.setItem('whatlyScheduledJob', JSON.stringify({id: %1, text: %2}));
    window.location.href = %3;
  } catch (e) { /* nothing we can do from here */ }
})();
)JS")
      .arg(jsString(id), jsString(text), jsString(url));
}
