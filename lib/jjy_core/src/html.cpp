#include "html.h"

#include <stdio.h>

#include "settings_rules.h"
#include "timezones.h"

namespace {

const char kCss[] =
    ":root{color-scheme:light dark;--bg:#f6f7f9;--card:#fff;--text:#1b1f24;--muted:#5b6470;--line:#d9dde3;"
    "--accent:#2563eb;--on-accent:#fff;--danger:#b42318;--ok:#067647}"
    "@media(prefers-color-scheme:dark){:root{--bg:#111418;--card:#1b2027;--text:#e8eaed;--muted:#9aa3ad;"
    "--line:#2e3540;--accent:#5b8def;--on-accent:#0b0d10;--danger:#f97066;--ok:#47cd89}}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--text);"
    "font:16px/1.5 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    "main{max-width:480px;margin:0 auto;padding:16px}"
    "h1{font-size:22px;margin:8px 0 16px}h2{font-size:17px;margin:0 0 4px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}"
    "label{display:block;font-weight:600;margin:12px 0 4px}"
    "input,select,button{font:inherit;width:100%;padding:12px;border:1px solid var(--line);border-radius:8px;"
    "background:var(--card);color:var(--text)}"
    "button{background:var(--accent);color:var(--on-accent);border:0;font-weight:600;margin-top:12px;cursor:pointer}"
    "button.secondary{background:transparent;color:var(--accent);border:1px solid var(--line)}"
    "button.danger{background:transparent;color:var(--danger);border:1px solid var(--line)}"
    ".row{display:flex;gap:8px;margin-bottom:8px}.row input{flex:1}"
    ".row button{width:auto;margin:0;padding:12px 16px}"
    ".help{color:var(--muted);font-size:14px;margin:6px 0 0}"
    ".msg{padding:12px;border-radius:8px;margin:0 0 16px;border:1px solid var(--line);background:var(--card)}"
    ".msg.error{color:var(--danger)}.msg.ok{color:var(--ok)}"
    ".big{font-size:32px;font-weight:700;line-height:1.2}"
    "summary{cursor:pointer;font-weight:600}a{color:var(--accent)}";

const char kScheduleScript[] =
    "var slots=document.getElementById('slots');"
    "function slotRm(b){if(slots.children.length>1)slots.removeChild(b.parentNode)}"
    "document.getElementById('slotAdd').onclick=function(){if(slots.children.length>=8)return;"
    "var r=slots.children[0].cloneNode(true);r.querySelector('input').value='';slots.appendChild(r)};";

std::string page(const std::string &title, const std::string &body, const std::string &script) {
  std::string out;
  out.reserve(body.size() + script.size() + sizeof(kCss) + 300);
  out += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>";
  out += htmlEscape(title);
  out += "</title><style>";
  out += kCss;
  out += "</style></head><body><main>";
  out += body;
  out += "</main>";
  if (!script.empty()) {
    out += "<script>";
    out += script;
    out += "</script>";
  }
  out += "</body></html>";
  return out;
}

std::string message(const std::string &text, bool isError) {
  if (text.empty()) return "";
  std::string out = "<div class=\"msg ";
  out += isError ? "error" : "ok";
  out += "\" role=\"status\">";
  out += htmlEscape(text);
  out += "</div>";
  return out;
}

std::string timezoneFields(const std::string &tzId, const std::string &customRule) {
  std::string out = "<label for=\"tz\">Time zone</label><select id=\"tz\" name=\"tz\">";
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    const TzEntry &e = kTimezones[i];
    out += "<option value=\"";
    out += htmlEscape(e.id);
    out += "\" data-iana=\"";
    out += htmlEscape(e.iana);
    out += "\"";
    if (tzId == e.id) out += " selected";
    out += ">";
    out += htmlEscape(e.label);
    out += "</option>";
  }
  out += "<option value=\"custom\"";
  if (tzId == "custom") out += " selected";
  out += ">Custom rule (advanced)</option></select>";
  out += "<p class=\"help\">Your clock will show the time of the city you pick.</p>";
  out += "<div id=\"tzc\" hidden><label for=\"tz_custom\">Custom POSIX rule</label>"
         "<input id=\"tz_custom\" name=\"tz_custom\" autocapitalize=\"none\" value=\"";
  out += htmlEscape(customRule);
  out += "\"></div>";
  return out;
}

std::string timezoneScript(bool autoDetect) {
  std::string s =
      "var tz=document.getElementById('tz'),tzc=document.getElementById('tzc');"
      "function tzUpd(){tzc.hidden=tz.value!=='custom'}tz.onchange=tzUpd;";
  if (autoDetect) {
    s += "try{var z=' '+Intl.DateTimeFormat().resolvedOptions().timeZone+' ';"
         "for(var i=0;i<tz.options.length;i++){var a=tz.options[i].getAttribute('data-iana');"
         "if(a&&(' '+a+' ').indexOf(z)>=0){tz.selectedIndex=i;break}}}catch(e){}";
  }
  s += "tzUpd();";
  return s;
}

std::string scheduleFields(const Schedule &schedule) {
  std::string out = "<label>Start times</label><div id=\"slots\">";
  const uint8_t rows = schedule.count > 0 ? schedule.count : 1;
  for (uint8_t i = 0; i < rows && i < kMaxSlots; ++i) {
    out += "<div class=\"row\"><input type=\"time\" name=\"slot\" required value=\"";
    if (i < schedule.count) out += formatHHMM(schedule.slotMinutes[i]);
    out += "\"><button type=\"button\" class=\"secondary\" aria-label=\"Remove this time\" "
           "onclick=\"slotRm(this)\">&#10005;</button></div>";
  }
  out += "</div><button type=\"button\" class=\"secondary\" id=\"slotAdd\">Add a time</button>";
  char window[8];
  snprintf(window, sizeof(window), "%u", static_cast<unsigned>(schedule.windowMinutes));
  out += "<label for=\"win\">Broadcast length (minutes)</label>"
         "<input id=\"win\" name=\"win\" type=\"number\" inputmode=\"numeric\" min=\"1\" max=\"30\" required value=\"";
  out += window;
  out += "\"><p class=\"help\">Radio clocks usually check the time at night. "
         "If you don't know when yours does, keep these times.</p>";
  return out;
}

}  // namespace

std::string htmlEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '<':
        out += "\\u003c";
        break;
      case '>':
        out += "\\u003e";
        break;
      case '&':
        out += "\\u0026";
        break;
      default:
        if (u < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", u);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string renderSetupPage(const SetupPageView &v) {
  std::string b = "<h1>Set up your JJY clock transmitter</h1>";
  b += message(v.error, true);
  b += "<form method=\"post\" action=\"/setup\">";
  b += "<section class=\"card\"><h2>1. WiFi</h2><label for=\"ssid\">Network</label><select id=\"ssid\" name=\"ssid\">";
  bool matched = false;
  for (const std::string &network : v.networks) {
    const bool selected = !matched && network == v.selectedSsid;
    matched = matched || selected;
    b += "<option value=\"";
    b += htmlEscape(network);
    b += "\"";
    if (selected) b += " selected";
    b += ">";
    b += htmlEscape(network);
    b += "</option>";
  }
  b += "<option value=\"\"";
  if (!matched) b += " selected";
  b += ">Other network&hellip;</option></select>";
  b += "<div id=\"other\" hidden><label for=\"ssid_other\">Network name</label>"
       "<input id=\"ssid_other\" name=\"ssid_other\" autocapitalize=\"none\" autocorrect=\"off\" value=\"";
  if (!matched) b += htmlEscape(v.selectedSsid);
  b += "\"></div>";
  b += "<label for=\"pass\">Password</label><input id=\"pass\" name=\"pass\" type=\"password\">";
  b += "<p class=\"help\"><a href=\"/?rescan=1\">Don't see your network? Refresh the list</a></p></section>";
  b += "<section class=\"card\"><h2>2. Where are you?</h2>";
  b += timezoneFields(v.tzId, v.customTzRule);
  b += "</section><section class=\"card\"><h2>3. When does your clock check the time?</h2>";
  b += scheduleFields(v.schedule);
  b += "</section><button type=\"submit\">Connect</button></form>";

  std::string script =
      "var ss=document.getElementById('ssid'),so=document.getElementById('other');"
      "function ssUpd(){so.hidden=ss.value!==''}ss.onchange=ssUpd;ssUpd();";
  script += timezoneScript(v.autoDetectTz);
  script += kScheduleScript;
  return page("JJY setup", b, script);
}

std::string renderConnectingPage() {
  const std::string body =
      "<div id=\"st\" class=\"card\"><h1>Connecting&hellip;</h1>"
      "<p>This takes up to 20 seconds. Keep this page open.</p>"
      "<p class=\"help\">If this page closes, join the JJY-Setup network again to see the result.</p></div>";
  const std::string script =
      "function poll(){fetch('/setup/status').then(function(r){return r.json()}).then(function(s){"
      "if(s.state==='connected'){var st=document.getElementById('st');"
      "st.innerHTML='<h1>Done!</h1><p>The transmitter is connected to your WiFi.</p>"
      "<p>To change settings later, connect your phone to your home WiFi and open "
      "<a href=\"http://jjy.local\">http://jjy.local</a></p>"
      "<p class=\"help\">If that address does not open, use <span id=\"ip\"></span></p>"
      "<p>This setup network turns off in a moment.</p>';"
      "document.getElementById('ip').textContent='http://'+s.ip;return}"
      "if(s.state==='failed'){location.href='/?failed=1';return}"
      "setTimeout(poll,2000)}).catch(function(){setTimeout(poll,2000)})}"
      "setTimeout(poll,2000);";
  return page("Connecting", body, script);
}

std::string renderSettingsPage(const SettingsPageView &v) {
  std::string b = "<h1>JJY clock transmitter</h1>";
  b += message(v.error, true);
  b += message(v.notice, false);
  b += "<section class=\"card\">";
  if (v.localTime.empty()) {
    b += "<p>Waiting for the time from the internet&hellip;</p>";
  } else {
    b += "<div class=\"big\">";
    b += htmlEscape(v.localTime);
    b += "</div>";
    if (v.broadcasting) {
      b += "<p>Broadcasting now</p>";
    } else if (!v.nextBroadcast.empty()) {
      b += "<p>Next broadcast at ";
      b += htmlEscape(v.nextBroadcast);
      b += "</p>";
    }
  }
  b += "</section>";

  b += "<form method=\"post\" action=\"/save\">";
  b += "<section class=\"card\"><h2>Time zone</h2>";
  b += timezoneFields(v.tzId, v.customTzRule);
  b += "</section><section class=\"card\"><h2>When does your clock check the time?</h2>";
  b += scheduleFields(v.schedule);
  b += "</section>";
  char freq[16];
  snprintf(freq, sizeof(freq), "%lu", static_cast<unsigned long>(v.freqHz));
  b += "<details class=\"card\"><summary>Advanced</summary>"
       "<label for=\"freq\">Carrier frequency (Hz)</label>"
       "<input id=\"freq\" name=\"freq\" type=\"number\" min=\"39500\" max=\"40500\" step=\"10\" value=\"";
  b += freq;
  b += "\"><p class=\"help\">Only change this if you are tuning the antenna.</p></details>";
  b += "<button type=\"submit\">Save</button></form>";

  b += "<section class=\"card\"><h2>Device</h2><p>Software version ";
  b += htmlEscape(v.version);
  b += "</p>";
  if (!v.updateVersion.empty()) {
    b += "<form method=\"post\" action=\"/update/install\"><button type=\"submit\">Install version ";
    b += htmlEscape(v.updateVersion);
    b += "</button></form>";
  } else {
    b += "<form method=\"post\" action=\"/update/check\">"
         "<button type=\"submit\" class=\"secondary\">Check for updates</button></form>";
  }
  b += "<form method=\"post\" action=\"/forget\" onsubmit=\"return confirm("
       "'Forget this WiFi network? You will need to set the transmitter up again.')\">"
       "<button type=\"submit\" class=\"danger\">Forget WiFi</button></form></section>";

  std::string script = timezoneScript(false);
  script += kScheduleScript;
  return page("JJY clock transmitter", b, script);
}

std::string renderUpdatingPage(const std::string &currentVersion) {
  const std::string body =
      "<div id=\"st\" class=\"card\"><h1>Updating&hellip;</h1>"
      "<p>Don't unplug the transmitter. This takes about a minute.</p></div>";
  std::string script = "var cur=\"";
  script += jsonEscape(currentVersion);
  script +=
      "\",t0=Date.now();"
      "function show(h,t){var st=document.getElementById('st');st.innerHTML=h;"
      "if(t){var p=document.createElement('p');p.textContent=t;st.appendChild(p)}}"
      "function next(){if(Date.now()-t0>180000){show('<h1>This is taking too long</h1>',"
      "'Unplug the transmitter, plug it back in, then open this page again.');return}setTimeout(poll,3000)}"
      "function poll(){fetch('/status').then(function(r){return r.json()}).then(function(s){"
      "if(s.version!==cur){show('<h1>Updated</h1><p><a href=\"/\">Back to settings</a></p>',"
      "'Now running version '+s.version+'.');return}"
      "if(s.ota==='failed'){show('<h1>Update failed</h1><p><a href=\"/\">Back to settings</a></p>',s.error);return}"
      "next()}).catch(next)}"
      "setTimeout(poll,5000);";
  return page("Updating", body, script);
}

std::string renderMessagePage(const std::string &title, const std::string &text) {
  std::string b = "<div class=\"card\"><h1>";
  b += htmlEscape(title);
  b += "</h1><p>";
  b += htmlEscape(text);
  b += "</p></div>";
  return page(title, b, "");
}
