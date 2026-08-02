#include "chatexport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace ChatExport {

QString collectorScript() {
  // Verified live against WhatsApp Web: rewinds the virtualised message pane to
  // the top, then walks down harvesting each rendered window (dedup by data-id)
  // and turning loaded media into data URLs while the row is still in the DOM.
  return QString::fromLatin1(R"JS(
(function () {
  'use strict';
  var WITH_MEDIA = true;
  window.__whatlyExport = { status: 'running', progress: 'starting', count: 0 };
  function sleep(ms){ return new Promise(function(r){ setTimeout(r, ms); }); }
  function scrollPane(){
    var main = document.querySelector('#main');
    if(!main) return null;
    var best=null;
    main.querySelectorAll('div').forEach(function(d){
      if(d.scrollHeight>d.clientHeight+50 && d.clientHeight>200){
        if(!best || d.clientHeight>best.clientHeight) best=d;
      }
    });
    return best;
  }
  function parsePre(pre){
    var m=/^\[([^\]]+)\]\s*(.*?):\s*$/.exec(pre||'');
    if(!m) return null;
    return { ts:m[1].trim(), sender:m[2].trim() };
  }
  function blobToDataUrl(url){
    return fetch(url).then(function(r){return r.blob();}).then(function(b){
      return new Promise(function(res){
        var fr=new FileReader();
        fr.onload=function(){ res({dataUrl:fr.result, mime:b.type||''}); };
        fr.onerror=function(){ res(null); };
        fr.readAsDataURL(b);
      });
    }).catch(function(){ return null; });
  }
  function readRow(row, state){
    if(row.querySelector('[data-icon="tail-out"]')) state.lastDir='out';
    else if(row.querySelector('[data-icon="tail-in"]')) state.lastDir='in';
    var rec={ type:'text', text:'', sender:'', ts:'', dir:state.lastDir };
    var preEl=row.querySelector('[data-pre-plain-text]');
    if(preEl){
      var pp=parsePre(preEl.getAttribute('data-pre-plain-text'));
      if(pp){ rec.ts=pp.ts; rec.sender=pp.sender;
        var dm=/(\d{1,2}\/\d{1,2}\/\d{2,4})/.exec(pp.ts); if(dm) state.lastDate=dm[1]; }
    }
    var txtEl=row.querySelector('.selectable-text, span.copyable-text .selectable-text');
    if(txtEl) rec.text=(txtEl.innerText||'');
    var img=row.querySelector('img[src^="blob:"]');
    var vid=row.querySelector('video');
    var aud=row.querySelector('audio');
    if(vid && vid.src && vid.src.indexOf('blob:')===0){ rec.type='video'; rec._blob=vid.src; }
    else if(aud && aud.src && aud.src.indexOf('blob:')===0){ rec.type='audio'; rec._blob=aud.src; }
    else if(img){ rec.type='image'; rec._blob=img.getAttribute('src'); }
    if(!rec.ts){
      var t=null;
      row.querySelectorAll('span').forEach(function(s){ var v=(s.innerText||'').trim(); if(/^\d{1,2}:\d{2}$/.test(v)) t=v; });
      rec.ts=(t||'')+(state.lastDate?', '+state.lastDate:'');
      if(!rec.sender) rec.sender=rec.dir==='out'?'':state.chatName;
    }
    return rec;
  }
  async function run(){
    var pane=scrollPane();
    var header=document.querySelector('#main header');
    var chatName=header?(header.innerText||'').split('\n')[0].trim():'chat';
    var state={ lastDir:'in', lastDate:'', chatName:chatName };
    var seen=new Map();
    function rowId(row){ var el=row.querySelector('[data-id]'); return el?el.getAttribute('data-id'):row.getAttribute('data-id'); }
    async function harvest(){
      var rows=Array.from(document.querySelectorAll('#main [role="row"]'));
      for(var i=0;i<rows.length;i++){
        var id=rowId(rows[i]);
        if(!id || seen.has(id)) continue;
        var rec=readRow(rows[i], state);
        if(!(rec.text || rec._blob || rec.type!=='text')) continue;
        if(WITH_MEDIA && rec._blob){ var m=await blobToDataUrl(rec._blob); if(m) rec.media=m; }
        delete rec._blob;
        seen.set(id, rec);
        window.__whatlyExport.count=seen.size;
      }
    }
    if(!pane){ await harvest(); }
    else {
      var stable=0, prev=-1;
      for(var a=0;a<400 && stable<3;a++){
        pane.scrollTop=0; await sleep(320);
        var h=pane.scrollHeight;
        if(h===prev) stable++; else { stable=0; prev=h; }
        window.__whatlyExport.progress='rewind '+a;
      }
      await harvest();
      var step=Math.max(200, Math.floor(pane.clientHeight*0.7)), guard=0;
      while(guard++<4000){
        var atBottom=pane.scrollTop+pane.clientHeight>=pane.scrollHeight-4;
        pane.scrollTop=Math.min(pane.scrollHeight, pane.scrollTop+step);
        await sleep(220); await harvest();
        window.__whatlyExport.progress='collect '+seen.size;
        if(atBottom) break;
      }
    }
    return { chat:chatName, messages:Array.from(seen.values()) };
  }
  (async function(){
    try { var res=await run();
      window.__whatlyExport={ status:'done', chat:res.chat, messages:res.messages, count:res.messages.length };
    } catch(e){ window.__whatlyExport={ status:'error', error:String((e&&e.message)||e) }; }
  })();
  return 'kicked';
})();
)JS");
}

QString statusScript() {
  return QStringLiteral(
      "(function(){var e=window.__whatlyExport;return e?JSON.stringify("
      "{status:e.status,progress:e.progress||'',count:e.count||0,"
      "error:e.error||''}):'{\"status\":\"none\"}';})();");
}

QString sanitizeFileName(const QString &name) {
  QString s = name;
  s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")),
            QStringLiteral("_"));
  s = s.trimmed();
  while (s.endsWith(QLatin1Char('.')))
    s.chop(1);
  if (s.isEmpty())
    s = QStringLiteral("chat");
  if (s.size() > 120)
    s = s.left(120);
  return s;
}

QString extForMime(const QString &mime) {
  const QString m = mime.toLower();
  if (m == QLatin1String("image/jpeg") || m == QLatin1String("image/jpg"))
    return QStringLiteral(".jpg");
  if (m == QLatin1String("image/png"))
    return QStringLiteral(".png");
  if (m == QLatin1String("image/webp"))
    return QStringLiteral(".webp");
  if (m == QLatin1String("image/gif"))
    return QStringLiteral(".gif");
  if (m == QLatin1String("video/mp4"))
    return QStringLiteral(".mp4");
  if (m == QLatin1String("video/webm"))
    return QStringLiteral(".webm");
  if (m == QLatin1String("audio/ogg") || m == QLatin1String("audio/ogg; codecs=opus"))
    return QStringLiteral(".ogg");
  if (m == QLatin1String("audio/mpeg"))
    return QStringLiteral(".mp3");
  if (m == QLatin1String("audio/mp4") || m == QLatin1String("audio/aac"))
    return QStringLiteral(".m4a");
  if (m == QLatin1String("application/pdf"))
    return QStringLiteral(".pdf");
  // Fall back to the subtype if it looks like a bare extension.
  const int slash = m.indexOf(QLatin1Char('/'));
  if (slash >= 0) {
    QString sub = m.mid(slash + 1);
    const int semi = sub.indexOf(QLatin1Char(';'));
    if (semi >= 0)
      sub = sub.left(semi);
    if (!sub.isEmpty() && sub.size() <= 5 &&
        sub == QString(sub).remove(QRegularExpression(QStringLiteral("[^a-z0-9]"))))
      return QLatin1Char('.') + sub;
  }
  return QStringLiteral(".bin");
}

QByteArray decodeDataUrl(const QString &dataUrl, QString *mime) {
  if (mime)
    mime->clear();
  const QRegularExpression re(QStringLiteral("^data:([^;,]*)(;base64)?,(.*)$"),
                              QRegularExpression::DotMatchesEverythingOption);
  const QRegularExpressionMatch m = re.match(dataUrl);
  if (!m.hasMatch())
    return QByteArray();
  if (mime)
    *mime = m.captured(1);
  const QString payload = m.captured(3);
  if (!m.captured(2).isEmpty())
    return QByteArray::fromBase64(payload.toLatin1());
  return QByteArray::fromPercentEncoding(payload.toUtf8());
}

QList<Message> parse(const QJsonArray &arr, QHash<QString, QByteArray> *mediaData) {
  QList<Message> out;
  int mediaSeq = 0;
  for (const QJsonValue &v : arr) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    Message m;
    m.ts = o.value(QStringLiteral("ts")).toString();
    m.sender = o.value(QStringLiteral("sender")).toString();
    m.direction = o.value(QStringLiteral("dir")).toString();
    m.type = o.value(QStringLiteral("type")).toString(QStringLiteral("text"));
    m.text = o.value(QStringLiteral("text")).toString();
    const QJsonValue media = o.value(QStringLiteral("media"));
    if (media.isObject()) {
      QString mime;
      const QByteArray bytes =
          decodeDataUrl(media.toObject().value(QStringLiteral("dataUrl")).toString(),
                        &mime);
      if (!bytes.isEmpty()) {
        if (mime.isEmpty())
          mime = media.toObject().value(QStringLiteral("mime")).toString();
        const QString name =
            QStringLiteral("%1%2")
                .arg(++mediaSeq, 4, 10, QLatin1Char('0'))
                .arg(extForMime(mime));
        m.mediaFile = name;
        if (mediaData)
          mediaData->insert(name, bytes);
      } else {
        m.mediaMissing = true;
      }
    } else if (m.type != QLatin1String("text")) {
      m.mediaMissing = true;
    }
    out.append(m);
  }
  return out;
}

QString transcriptLine(const Message &m) {
  QString body;
  if (m.type == QLatin1String("text")) {
    body = m.text;
  } else if (!m.mediaFile.isEmpty()) {
    body = QStringLiteral("<attached: media/%1>").arg(m.mediaFile);
    if (!m.text.isEmpty())
      body += QLatin1Char(' ') + m.text;
  } else {
    body = QStringLiteral("<%1 omitted>").arg(m.type);
    if (!m.text.isEmpty())
      body += QLatin1Char(' ') + m.text;
  }
  const QString who = m.sender.isEmpty()
                          ? (m.direction == QLatin1String("out")
                                 ? QStringLiteral("You")
                                 : QString())
                          : m.sender;
  QString line = QStringLiteral("[%1] ").arg(m.ts);
  if (!who.isEmpty())
    line += who + QStringLiteral(": ");
  return line + body;
}

QString buildTranscript(const QString &chatName, const QList<Message> &msgs) {
  QStringList lines;
  lines << QStringLiteral("WhatsApp chat with %1").arg(chatName);
  lines << QStringLiteral("Exported by Whatly (%1 messages)").arg(msgs.size());
  lines << QString();
  for (const Message &m : msgs)
    lines << transcriptLine(m);
  return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QByteArray buildJson(const QList<Message> &msgs) {
  QJsonArray arr;
  for (const Message &m : msgs) {
    QJsonObject o;
    o.insert(QStringLiteral("ts"), m.ts);
    o.insert(QStringLiteral("sender"), m.sender);
    o.insert(QStringLiteral("direction"), m.direction);
    o.insert(QStringLiteral("type"), m.type);
    o.insert(QStringLiteral("text"), m.text);
    if (!m.mediaFile.isEmpty())
      o.insert(QStringLiteral("media"),
               QStringLiteral("media/%1").arg(m.mediaFile));
    else if (m.mediaMissing)
      o.insert(QStringLiteral("media"), QJsonValue::Null);
    arr.append(o);
  }
  return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

} // namespace ChatExport
