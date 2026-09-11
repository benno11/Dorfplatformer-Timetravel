"use strict";
const q = id => document.getElementById(id);
let adminKey = "", moderatorToken = "", state = null, player = null, level = null;
let usersOffset = 0, levelsOffset = 0, busy = false;
const make = (tag, text, className) => {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  if (className) node.className = className;
  return node;
};
function status(message, error = false) { q("status").textContent = message; q("status").className = error ? "error" : ""; }
async function api(path, body) {
  const response = await fetch("/admin/api/" + path, {
    method: body === undefined ? "GET" : "POST",
    headers: { "Content-Type": "application/json", Authorization: "Bearer " + adminKey,
      ...(moderatorToken ? { "X-Moderator-Token": moderatorToken } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body)
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error?.message || "Request failed.");
  return result;
}
async function perform(action) {
  if (busy) return;
  busy = true;
  document.body.setAttribute("aria-busy", "true");
  try { await action(); } catch (error) { status(error.message, true); }
  finally { busy = false; document.body.setAttribute("aria-busy", "false"); }
}
const bind = (id, action) => q(id).addEventListener("click", () => perform(action));
function row(cells, action) {
  const tr = make("tr");
  for (const value of cells) tr.appendChild(make("td", String(value)));
  if (action) {
    const td = make("td"), button = make("button", action.label);
    button.addEventListener("click", () => perform(action.run));
    td.appendChild(button); tr.appendChild(td);
  }
  return tr;
}
function selectPlayer(value) {
  player = value;
  q("playerControls").hidden = !player;
  q("playerTitle").textContent = player ? player.username : "Select a player";
  if (!player) return;
  q("playerDetails").textContent = player.email + " · " + player.level_count + " levels";
  q("toggleModerator").textContent = player.is_moderator ? "Revoke moderator" : "Grant moderator";
  q("playerRestriction").textContent = player.upload_suspended
    ? "Uploads suspended " + (player.until ? "until " + new Date(player.until * 1000).toLocaleString() : "permanently") + ": " + player.reason
    : "Uploads allowed.";
  q("playerWarning").textContent = player.warning ? "Last warning: " + player.warning : "";
}
async function refresh() {
  state = await api("state?q=" + encodeURIComponent(q("search").value.trim()) +
    "&users_offset=" + usersOffset + "&levels_offset=" + levelsOffset);
  q("stats").replaceChildren();
  for (const [key, value] of Object.entries(state.stats)) {
    const stat = make("div", undefined, "stat");
    stat.append(make("strong", value), make("span", key));
    q("stats").appendChild(stat);
  }
  q("players").replaceChildren();
  for (const user of state.users) q("players").appendChild(row([
    user.username, (user.is_moderator ? "Moderator" : "Player") + (user.upload_suspended ? " / Suspended" : ""), user.level_count
  ], { label: "Manage", run: () => { selectPlayer(user); q("playerReason").value = ""; } }));
  q("levels").replaceChildren();
  for (const entry of state.levels) q("levels").appendChild(row([
    entry.name, entry.owner, entry.difficulty == null ? "Unrated" : entry.difficulty + "/9"
  ], { label: "Review", run: () => selectLevel(entry.id) }));
  q("blockedVersions").replaceChildren();
  for (const block of state.blocked_versions || []) q("blockedVersions").appendChild(row([
    block.version_id, block.reason, new Date(block.created_at * 1000).toLocaleString()
  ]));
  q("events").replaceChildren();
  for (const event of state.events) q("events").appendChild(row([
    new Date(event.created_at * 1000).toLocaleString(), event.action, event.target_name || event.target_id, event.reason, event.actor_name
  ]));
  q("playersCount").textContent = state.users_total + " matches";
  q("levelsCount").textContent = state.levels_total + " matches";
  q("prevPlayers").disabled = usersOffset === 0; q("nextPlayers").disabled = usersOffset + 25 >= state.users_total;
  q("prevLevels").disabled = levelsOffset === 0; q("nextLevels").disabled = levelsOffset + 25 >= state.levels_total;
  if (player) selectPlayer(state.users.find(user => user.id === player.id) || null);
}
async function selectLevel(id) {
  level = await api("level?id=" + encodeURIComponent(id));
  q("levelControls").hidden = false;
  q("levelTitle").textContent = level.name;
  q("levelDetails").textContent = level.level_id + " · " + level.owner + " · " + (level.difficulty == null ? "Unrated" : level.difficulty + "/9");
  q("levelData").value = level.data;
  q("difficulty").value = String(level.difficulty || 1);
  q("deleteReason").value = "";
}
function reason(id) {
  const text = q(id).value.trim();
  if (text.length < 3) throw new Error("Enter a reason of at least 3 characters.");
  return text;
}
async function playerAction(action, extra = {}) {
  if (!player) throw new Error("Select a player.");
  await api(action, { user_id: player.id, reason: reason("playerReason"), ...extra });
  await refresh(); status("Player updated.");
}
q("unlockForm").addEventListener("submit", event => {
  event.preventDefault();
  perform(async () => {
    adminKey = q("adminKey").value.trim();
    try { await refresh(); } catch (error) { adminKey = ""; throw error; }
    q("adminKey").value = ""; q("login").hidden = true; q("dashboard").hidden = false; q("lock").hidden = false;
    status("Connected to the local dashboard.");
  });
});
q("searchForm").addEventListener("submit", event => { event.preventDefault(); perform(async () => {
  usersOffset = levelsOffset = 0; await refresh(); status("Dashboard refreshed.");
}); });
bind("lock", async () => {
  try { if (moderatorToken) await api("moderator-logout", {}); } finally {
    adminKey = moderatorToken = ""; player = level = state = null;
    q("login").hidden = false; q("dashboard").hidden = true; q("lock").hidden = true;
    q("moderatorPassword").value = ""; q("moderatorStatus").textContent = "Not signed in as a moderator.";
    q("playerControls").hidden = true; q("levelControls").hidden = true;
    q("levelData").value = ""; for (const id of ["players", "levels", "events", "stats", "blockedVersions"]) q(id).replaceChildren();
    status("Dashboard locked.");
  }
});
bind("prevPlayers", async () => { usersOffset = Math.max(0, usersOffset - 25); await refresh(); });
bind("nextPlayers", async () => { usersOffset += 25; await refresh(); });
bind("prevLevels", async () => { levelsOffset = Math.max(0, levelsOffset - 25); await refresh(); });
bind("nextLevels", async () => { levelsOffset += 25; await refresh(); });
bind("toggleModerator", () => playerAction("moderator", { enabled: !player?.is_moderator }));
bind("warnPlayer", () => playerAction("warn"));
bind("applySuspension", () => playerAction("suspension", { hours: q("suspension").value === "permanent" ? null : Number(q("suspension").value) }));
q("moderatorForm").addEventListener("submit", event => {
  event.preventDefault();
  perform(async () => {
    try {
      if (moderatorToken) { await api("moderator-logout", {}); moderatorToken = ""; }
      const session = await api("moderator-login", { email: q("moderatorEmail").value.trim(), password: q("moderatorPassword").value });
      moderatorToken = session.idToken;
      q("moderatorStatus").textContent = "Rating as " + session.displayName + ".";
      status("Moderator signed in.");
    } finally { q("moderatorPassword").value = ""; }
  });
});
bind("rateLevel", async () => {
  if (!level || !moderatorToken) throw new Error("Select a level and sign in as a moderator first.");
  const id = level.level_id;
  await api("rate", { level_id: id, difficulty: Number(q("difficulty").value) });
  await selectLevel(id); await refresh(); status("Difficulty saved.");
});
bind("deleteLevel", async () => {
  if (!level) throw new Error("Select a level.");
  const deletionReason = reason("deleteReason");
  if (!confirm("Permanently delete shared level '" + level.name + "' (" + level.level_id + ")? Local copies are unaffected.")) return;
  await api("delete-level", { level_id: level.level_id, reason: deletionReason });
  level = null; q("levelControls").hidden = true; q("levelTitle").textContent = "Select a level"; q("levelData").value = "";
  await refresh(); status("Level deleted.");
});
bind("downloadLevel", () => {
  if (!level) return;
  const url = URL.createObjectURL(new Blob([level.data], { type: "text/plain" }));
  const link = make("a"); link.href = url; link.download = level.level_id.replace(/[^a-zA-Z0-9_-]/g, "_") + ".txt";
  link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
});

async function versionBlock(enabled) {
  const value = q("versionId").value.trim();
  if (value !== "legacy" && (!/^[1-9][0-9]{0,9}$/.test(value) || Number(value) > 2100000000))
    throw new Error("Enter a build ID from 1 to 2100000000, or legacy.");
  await api("version-block", {version_id: value === "legacy" ? value : Number(value),
    enabled, reason: reason("versionReason")});
  await refresh();
  status(enabled ? "Client version blocked." : "Client version unblocked.");
}
bind("blockVersion", () => versionBlock(true));
bind("unblockVersion", () => versionBlock(false));
