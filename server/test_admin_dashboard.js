const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

test("local dashboard manages players, reviews and rates levels, confirms deletion and locks", async () => {
  class Element {
    constructor() { this.children = []; this.events = {}; this.value = ""; this.hidden = false; this.textContent = ""; }
    addEventListener(event, fn) { this.events[event] = fn; }
    appendChild(node) { this.children.push(node); return node; }
    append(...nodes) { this.children.push(...nodes); }
    replaceChildren(...nodes) { this.children = nodes; }
    setAttribute() {}
  }
  const elements = new Map();
  const q = id => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  const player = { id: "u1", username: "<player>", email: "p@example.com", level_count: 1,
    is_moderator: 0, upload_suspended: 0, warning: null };
  let levels = [{ id: "demo", level_id: "demo", name: "Demo", owner: "<player>", difficulty: null, data: "raw level data" }];
  let allowDelete = false;
  let minimumVersionPolicy = {minimum_build: 239, reason: "Update your game."};
  let updaterConfig = {enabled: false, command: [], cwd: "/srv/game", timeout_seconds: 900,
    manifest_url: "https://updates.example.com/manifest.json"};
  const requests = [];
  const context = {
    document: { getElementById: q, createElement: () => new Element(), body: new Element() },
    confirm: () => allowDelete,
    fetch: async (url, options) => {
      const body = options.body ? JSON.parse(options.body) : null;
      requests.push({ url, options, body });
      assert.equal(options.headers.Authorization, "Bearer local-secret-key");
      const route = url.split("?")[0];
      let result = { ok: true };
      if (route === "/admin/api/updater" || route === "/admin/api/updater/validate") {
        if (body && body.timeout_seconds < 1)
          return {ok: false, json: async () => ({error: {message: "Timeout must be positive."}})};
        if (body && route === "/admin/api/updater") updaterConfig = {...body};
        result = {config: body || updaterConfig, valid: true, saved: route === "/admin/api/updater" && !!body, last_run: null};
      }
      if (route === "/admin/api/moderator") player.is_moderator = body.enabled ? 1 : 0;
      if (route === "/admin/api/warn") player.warning = body.reason;
      if (route === "/admin/api/suspension") {
        player.upload_suspended = body.hours === 0 ? 0 : 1;
        player.reason = body.reason;
      }
      if (route === "/admin/api/moderator-login") result = { idToken: "moderator-token", displayName: "mod", isModerator: true };
      if (route === "/admin/api/rate") {
        assert.equal(options.headers["X-Moderator-Token"], "moderator-token");
        levels[0].difficulty = body.difficulty || null;
      }
      if (route === "/admin/api/delete-level") levels = [];
      if (route === "/admin/api/minimum-version") minimumVersionPolicy = {...body};
      if (route === "/admin/api/level") result = levels[0];
      if (route === "/admin/api/state") result = {
        users: [player], levels, events: [], stats: { players: 1, levels: levels.length, moderators: player.is_moderator, suspended: player.upload_suspended },
        users_total: 1, levels_total: levels.length, minimum_version_policy: minimumVersionPolicy
      };
      return { ok: true, json: async () => JSON.parse(JSON.stringify(result)) };
    }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "admin/admin.js"), "utf8"), context);
  const trigger = async (element, type = "click") => {
    element.events[type]({ preventDefault() {} });
    await new Promise(setImmediate);
  };
  q("adminKey").value = "local-secret-key";
  await trigger(q("unlockForm"), "submit");
  assert.equal(q("dashboard").hidden, false);
  assert.equal(q("updaterEnabled").value, "disabled");
  assert.equal(q("updaterCwd").value, "/srv/game");
  q("updaterEnabled").value = "enabled";
  q("updaterExecutable").value = "/path with spaces/python";
  q("updaterArguments").value = "update.py\n--branch\nrelease next";
  q("updaterTimeout").value = "300";
  await trigger(q("updaterArguments"), "input");
  assert.match(q("updaterPreview").textContent, /release next/);
  await trigger(q("validateUpdater"));
  assert.equal(updaterConfig.enabled, false); // Validation never saves.
  assert.match(q("updaterStatus").textContent, /Nothing was saved or run/);
  await trigger(q("saveUpdater"));
  assert.equal(updaterConfig.enabled, true);
  assert.deepEqual(updaterConfig.command, ["/path with spaces/python", "update.py", "--branch", "release next"]);
  assert.equal(updaterConfig.timeout_seconds, 300);
  assert.match(q("updaterStatus").textContent, /No update was run/);
  q("updaterTimeout").value = "0";
  await trigger(q("validateUpdater"));
  assert.match(q("updaterStatus").textContent, /Timeout must be positive/);
  await trigger(q("reloadUpdater"));
  assert.equal(q("updaterTimeout").value, "300");
  assert.equal(q("adminKey").value, "");
  assert.equal(q("minimumVersion").value, "239");
  assert.equal(q("minimumVersionRange").value, "239");
  assert.match(q("minimumVersionStatus").textContent, /239/);
  q("minimumVersionRange").value = "250";
  const beforeMinimumEdit = requests.length;
  await trigger(q("minimumVersionRange"), "input");
  assert.equal(q("minimumVersion").value, "250");
  assert.equal(requests.length, beforeMinimumEdit);
  q("minimumVersionReason").value = "New level format";
  await trigger(q("applyMinimumVersion"));
  assert.deepEqual(requests.at(-2).body, {minimum_build: 250, reason: "New level format"});
  assert.equal(q("minimumVersion").value, "250");
  q("minimumVersion").value = "239";
  await trigger(q("minimumVersion"), "input");
  assert.equal(q("minimumVersionRange").value, "239");
  await trigger(q("applyMinimumVersion"));
  assert.equal(minimumVersionPolicy.minimum_build, 239);
  q("minimumVersion").value = "-1";
  const beforeInvalidMinimum = requests.length;
  await trigger(q("applyMinimumVersion"));
  assert.equal(requests.length, beforeInvalidMinimum);
  assert.match(q("status").textContent, /Enter a minimum build/);
  assert.equal(q("players").children[0].children[0].textContent, "<player>");
  await trigger(q("players").children[0].children[3].children[0]);
  q("playerReason").value = "Trusted reviewer";
  await trigger(q("toggleModerator"));
  assert.equal(player.is_moderator, 1);
  q("playerReason").value = "Inappropriate uploads";
  await trigger(q("warnPlayer"));
  assert.equal(player.warning, "Inappropriate uploads");
  q("suspension").value = "168";
  await trigger(q("applySuspension"));
  assert.equal(player.upload_suspended, 1);
  await trigger(q("levels").children[0].children[3].children[0]);
  assert.equal(q("levelData").value, "raw level data");
  await trigger(q("rateLevel"));
  assert.match(q("status").textContent, /sign in as a moderator/);
  q("moderatorEmail").value = "mod@example.com"; q("moderatorPassword").value = "password";
  await trigger(q("moderatorForm"), "submit");
  assert.equal(q("moderatorPassword").value, "");
  q("difficulty").value = "6";
  await trigger(q("rateLevel"));
  assert.equal(levels[0].difficulty, 6);
  q("difficulty").value = "0";
  await trigger(q("rateLevel"));
  assert.equal(levels[0].difficulty, null);
  assert.match(q("levelDetails").textContent, /Unrated/);
  assert.equal(q("difficulty").value, "0");
  q("deleteReason").value = "Breaks server rules";
  await trigger(q("deleteLevel"));
  assert.equal(levels.length, 1);
  allowDelete = true;
  await trigger(q("deleteLevel"));
  assert.equal(levels.length, 0);
  q("versionId").value = "27";
  q("versionReason").value = "Broken uploads";
  await trigger(q("blockVersion"));
  assert.deepEqual(requests.at(-2).body, {version_id: 27, enabled: true, reason: "Broken uploads"});
  await trigger(q("unblockVersion"));
  assert.equal(requests.at(-2).body.enabled, false);
  q("versionId").value = "invalid";
  const previousRequests = requests.length;
  await trigger(q("blockVersion"));
  assert.equal(requests.length, previousRequests);
  assert.match(q("status").textContent, /Enter a build ID/);
  await trigger(q("lock"));
  assert.equal(q("dashboard").hidden, true);
  assert.equal(q("levelData").value, "");
  assert.ok(requests.some(r => r.url === "/admin/api/moderator-logout"));
});
