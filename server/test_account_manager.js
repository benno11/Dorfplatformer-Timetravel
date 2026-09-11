const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

test("account manager registration, account update and logout use the custom server", async () => {
  const elements = new Map();
  const storage = new Map();
  const requests = [];
  const q = id => {
    if (!elements.has(id)) elements.set(id, {
      value: "", hidden: false, disabled: false, textContent: "",
      addEventListener(name, callback) { this[name] = callback; }
    });
    return elements.get(id);
  };
  const session = { idToken: "test-token", displayName: "player" };
  const context = {
    document: { getElementById: q },
    location: { origin: "http://localhost:8080" },
    sessionStorage: {
      getItem: key => storage.get(key),
      setItem: (key, value) => storage.set(key, value),
      removeItem: key => storage.delete(key)
    },
    fetch: async (url, options) => {
      requests.push({ url, ...options, body: JSON.parse(options.body) });
      return { ok: true, json: async () => session };
    }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../pages/account-manager.js"), "utf8"), context);
  q("createEmail").value = "player@example.com";
  q("createPassword").value = "strong-password-123";
  q("createUsername").value = "player";
  await q("createAccountBtn").click();
  assert.equal(requests[0].url, "/api/auth/register");
  assert.equal(requests[0].body.displayName, "player");
  assert.equal(q("createPassword").value, "");
  assert.equal(q("createPanel").hidden, true);
  q("currentPassword").value = "strong-password-123";
  q("newPassword").value = "another-password";
  await q("changePasswordBtn").click();
  assert.equal(requests[1].url, "/api/auth/update");
  assert.equal(requests[1].headers.Authorization, "Bearer test-token");
  assert.equal(requests[1].body.currentPassword, "strong-password-123");
  assert.equal(q("currentPassword").value, "");
  await q("signOutBtn").click();
  assert.equal(requests[2].url, "/api/auth/logout");
  assert.equal(storage.size, 0);
  assert.equal(q("loginPanel").hidden, false);
});
test("moderators can select a level and submit a 1-9 rating", async () => {
  const elements = new Map();
  const q = id => {
    if (!elements.has(id)) elements.set(id, {
      value: "", hidden: false, disabled: false, textContent: "", children: [],
      addEventListener(name, callback) { this[name] = callback; },
      replaceChildren() { this.children = []; this.value = ""; },
      appendChild(option) { this.children.push(option); if (!this.value) this.value = option.value; }
    });
    return elements.get(id);
  };
  const requests = [];
  let rating = null;
  const context = {
    document: { getElementById: q, createElement: () => ({}) },
    location: { origin: "http://localhost:8080" },
    sessionStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    fetch: async (url, options) => {
      requests.push({ url, ...options });
      if (url === "/api/auth/login")
        return { ok: true, json: async () => ({ idToken: "mod-token", displayName: "mod", isModerator: true }) };
      if (url === "/levels/demo/difficulty") rating = JSON.parse(options.body).difficulty;
      return { ok: true, json: async () => ({ demo: { name: "Demo", difficulty: rating } }) };
    }
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../pages/account-manager.js"), "utf8"), context);
  assert.equal(q("moderatorPanel").hidden, true);
  q("loginEmail").value = "mod@example.com";
  q("loginPassword").value = "strong-password";
  await q("signInBtn").click();
  assert.equal(q("moderatorPanel").hidden, false);
  await q("loadRatedLevelsBtn").click();
  assert.match(q("ratingLevel").children[0].textContent, /Unrated/);
  q("difficultyRating").value = "7";
  await q("setDifficultyBtn").click();
  const submission = requests.find(request => request.url === "/levels/demo/difficulty");
  assert.equal(submission.method, "PUT");
  assert.equal(submission.headers.Authorization, "Bearer mod-token");
  assert.deepEqual(JSON.parse(submission.body), { difficulty: 7 });
  assert.match(q("ratingLevel").children[0].textContent, /7\/9/);
  assert.equal(q("setDifficultyBtn").disabled, false);
});
