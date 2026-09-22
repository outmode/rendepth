let siteOrigin;
rendepthBrowser.tabs.query({active: true, currentWindow: true}).then(async ([tab]) => {
  if (tab?.url && /^https?:/.test(tab.url)) {
    const url = new URL(tab.url);
    siteOrigin = url.origin;
    const reply = await rendepthBrowser.runtime.sendMessage({action: "site-status", origin: siteOrigin});
    document.getElementById("auto-reconnect").checked = reply.autoReconnect;
    document.getElementById("auto-reconnect").disabled = false;
  }
  document.getElementById("open").disabled = false;
});
document.getElementById("auto-reconnect").onchange = async event => {
  const control = event.target;
  const enabled = control.checked;
  control.disabled = true;
  try {
    // Do not await before this call: permissions.request needs the checkbox gesture.
    // The service worker finishes saving even if the permission UI closes us.
    const reply = await rendepthBrowser.runtime.sendMessage({action: 'auto-reconnect', origin: siteOrigin, enabled});
    if (reply.error) throw new Error(reply.error);
    control.checked = reply.autoReconnect;
    document.getElementById("navigation-notice").textContent = enabled && !reply.autoReconnect
      ? "Automatic reconnect is off. You can still use Refresh Video after navigation." : "";
  } catch (_) {
    control.checked = false;
    document.getElementById("navigation-notice").textContent = "Could not enable automatic reconnect. Use Refresh Video after navigation.";
  } finally { control.disabled = false; }
};
async function refresh() {
  const reply = await rendepthBrowser.runtime.sendMessage({action: "status"});
  document.getElementById("status").textContent = reply.status;
  document.getElementById("quality-notice").textContent = reply.qualityNotice;
  document.getElementById("open").textContent = reply.active ? "Refresh Video" : "Open Playing Video";
  document.getElementById("stop").disabled = !reply.active;
}
document.getElementById("open").onclick = async () => {
  await savingPreference;
  await rendepthBrowser.runtime.sendMessage({action: "open",
    format: document.getElementById("format").value,
    scaleHalf: document.getElementById("scale-half").checked});
  await refresh();
};
const formatSelect = document.getElementById("format");
const formatPicker = document.getElementById("format-picker");
const formatToggle = document.getElementById("format-toggle");
const formatOptions = document.getElementById("format-options");
const options = Array.from(formatSelect.options, option => {
  const item = document.createElement("li");
  item.textContent = option.textContent;
  item.dataset.value = option.value;
  item.setAttribute("role", "option");
  item.tabIndex = -1;
  item.onclick = () => chooseFormat(item);
  formatOptions.appendChild(item);
  return item;
});

function closeFormats(returnFocus = false) {
  formatOptions.hidden = true;
  formatToggle.setAttribute("aria-expanded", "false");
  if (returnFocus) formatToggle.focus();
}

function openFormats(index = formatSelect.selectedIndex) {
  formatOptions.hidden = false;
  formatToggle.setAttribute("aria-expanded", "true");
  options[index].focus();
}

function chooseFormat(item) {
  formatSelect.value = item.dataset.value;
  formatSelect.dispatchEvent(new Event("change"));
  closeFormats(true);
}

formatToggle.onclick = () => formatOptions.hidden ? openFormats() : closeFormats();
formatPicker.onkeydown = event => {
  const current = options.indexOf(document.activeElement);
  let next;
  if (event.key === "ArrowDown") next = current < 0 ? formatSelect.selectedIndex : Math.min(current + 1, options.length - 1);
  else if (event.key === "ArrowUp") next = current < 0 ? formatSelect.selectedIndex : Math.max(current - 1, 0);
  else if (event.key === "Home") next = 0;
  else if (event.key === "End") next = options.length - 1;
  else if (event.key === "Escape" && !formatOptions.hidden) {
    event.preventDefault();
    event.stopPropagation();
    closeFormats(true);
  } else if ((event.key === "Enter" || event.key === " ") && current >= 0) {
    event.preventDefault();
    chooseFormat(options[current]);
  } else if (event.key === "Tab" && current >= 0) {
    // Return to the trigger before normal tab navigation leaves the picker.
    closeFormats(true);
  } else if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) {
    const start = current < 0 ? formatSelect.selectedIndex : current;
    for (let offset = 1; offset <= options.length; ++offset) {
      const index = (start + offset) % options.length;
      if (options[index].textContent.toLowerCase().startsWith(event.key.toLowerCase())) {
        next = index;
        break;
      }
    }
  }
  if (next !== undefined) {
    event.preventDefault();
    openFormats(next);
  }
};
document.addEventListener("pointerdown", event => {
  if (!formatPicker.contains(event.target)) closeFormats();
});
formatPicker.addEventListener("focusout", event => {
  if (!formatPicker.contains(event.relatedTarget)) closeFormats();
});

formatSelect.onchange = () => {
  const format = formatSelect.value;
  document.getElementById("format-value").textContent = formatSelect.selectedOptions[0].textContent;
  for (const item of options) item.setAttribute("aria-selected", String(item.dataset.value === format));
  document.getElementById("scale-half").disabled = format !== "sbs-half";
  document.getElementById("half-options").hidden = format !== "sbs-half";
  document.getElementById("mono-help").hidden = format !== "2d";
};
document.getElementById("stop").onclick = async () => {
  await rendepthBrowser.runtime.sendMessage({action: "stop"});
  await refresh();
};
document.getElementById("format").onchange();
rendepthBrowser.runtime.sendMessage({action: "status"}).then(reply => {
  if (reply.options) {
    formatSelect.value = reply.options.format;
    document.getElementById("scale-half").checked = reply.options.scaleHalf;
    formatSelect.onchange();
  }
  document.getElementById("limit-source").checked = reply.limitSource;
  document.getElementById("limit-source").disabled = false;
});
let savingPreference = Promise.resolve();
document.getElementById("limit-source").onchange = event => {
  const limitSource = event.target.checked;
  savingPreference = savingPreference.then(() => rendepthBrowser.runtime.sendMessage({action: "preferences", limitSource}));
};
refresh();
setInterval(refresh, 500);
