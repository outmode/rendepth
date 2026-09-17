async function refresh() {
  const reply = await browser.runtime.sendMessage({action: "status"});
  document.getElementById("status").textContent = reply.status;
  document.getElementById("stop").disabled = !reply.active;
}
document.getElementById("open").onclick = async () => {
  await browser.runtime.sendMessage({action: "open",
    format: document.getElementById("format").value,
    scaleHalf: document.getElementById("scale-half").checked,
    swap: document.getElementById("swap").checked});
  await refresh();
};
document.getElementById("format").onchange = () => {
  const format = document.getElementById("format").value;
  document.getElementById("scale-half").disabled = format !== "sbs-half";
  document.getElementById("half-options").hidden = format !== "sbs-half";
  document.getElementById("mono-help").hidden = format !== "2d";
};
document.getElementById("stop").onclick = async () => {
  await browser.runtime.sendMessage({action: "stop"});
  await refresh();
};
document.getElementById("format").onchange();
refresh();
setInterval(refresh, 500);
