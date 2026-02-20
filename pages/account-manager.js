(function () {
  "use strict";

  var API_DESCRIPTOR_PATH = "./api.json";
  var FIREBASE_API_KEY = "";
  var LEVEL_SERVER_URL = "";

  var STORAGE_KEY = "dfnew_account_settings_v2";

  function q(id) { return document.getElementById(id); }

  var createEmailEl = q("createEmail");
  var createPasswordEl = q("createPassword");
  var createUsernameEl = q("createUsername");

  var loginEmailEl = q("loginEmail");
  var loginPasswordEl = q("loginPassword");

  var newUsernameEl = q("newUsername");
  var newPasswordEl = q("newPassword");

  var createAccountBtn = q("createAccountBtn");
  var signInBtn = q("signInBtn");
  var signOutBtn = q("signOutBtn");
  var changeUsernameBtn = q("changeUsernameBtn");
  var changePasswordBtn = q("changePasswordBtn");

  var statusEl = q("status");
  var sessionSummaryEl = q("sessionSummary");

  var session = {
    email: "",
    localId: "",
    idToken: "",
    refreshToken: "",
    level_server_account_username: "",
    level_server_auth_token: "",
    level_server_url: LEVEL_SERVER_URL,
    api_key: FIREBASE_API_KEY
  };

  function sanitizeUsername(raw) {
    return (raw || "").replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 48);
  }

  function setStatus(msg, type) {
    statusEl.textContent = msg;
    statusEl.className = "status";
    if (type === "ok") statusEl.classList.add("ok");
    if (type === "err") statusEl.classList.add("err");
  }

  function readStored() {
    var raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    try { return JSON.parse(raw); } catch (_) { return null; }
  }

  function persist() {
    session.api_key = FIREBASE_API_KEY;
    session.level_server_url = LEVEL_SERVER_URL;
    localStorage.setItem(STORAGE_KEY, JSON.stringify(session));
  }

  function renderSummary() {
    var lines = [];
    lines.push("email: " + (session.email || "<none>"));
    lines.push("localId: " + (session.localId || "<none>"));
    lines.push("username: " + (session.level_server_account_username || "<none>"));
    lines.push("server: " + session.level_server_url);
    lines.push("token: " + (session.level_server_auth_token ? "configured" : "<none>"));
    lines.push("api_key: " + (session.api_key && session.api_key !== "REPLACE_WITH_FIREBASE_WEB_API_KEY" ? "configured" : "<placeholder>"));
    sessionSummaryEl.textContent = lines.join("\n");
  }

  function getApiKey() {
    session.api_key = FIREBASE_API_KEY;
    if (!session.api_key) {
      setStatus("Firebase API key missing in api.json.", "err");
      return "";
    }
    return session.api_key;
  }

  async function loadApiDescriptorConfig() {
    try {
      var res = await fetch(API_DESCRIPTOR_PATH, { method: "GET" });
      var text = await res.text();
      if (!res.ok) throw new Error("HTTP " + res.status);
      var json = text ? JSON.parse(text) : {};
      if (json && json.firebase && typeof json.firebase === "object") {
        if (typeof json.firebase.api_key === "string") {
          FIREBASE_API_KEY = json.firebase.api_key.trim();
        }
        if (typeof json.firebase.level_server_url === "string") {
          LEVEL_SERVER_URL = json.firebase.level_server_url.trim().replace(/\/+$/, "");
        }
      }
    } catch (err) {
      setStatus("Failed to load api.json config.", "err");
    }
  }

  async function firebasePost(path, body) {
    var apiKey = getApiKey();
    if (!apiKey) throw new Error("Missing API key");
    var url = "https://identitytoolkit.googleapis.com/v1/" + path + "?key=" + encodeURIComponent(apiKey);
    var res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    var text = await res.text();
    var json = {};
    try { json = text ? JSON.parse(text) : {}; } catch (_) {}
    if (!res.ok) {
      var msg = (json && json.error && json.error.message) ? json.error.message : ("HTTP " + res.status);
      throw new Error(msg);
    }
    return json;
  }

  function applyAuthResponse(authJson) {
    session.email = authJson.email || session.email || "";
    session.localId = authJson.localId || session.localId || "";
    session.idToken = authJson.idToken || session.idToken || "";
    session.refreshToken = authJson.refreshToken || session.refreshToken || "";
    session.level_server_auth_token = session.idToken || "";
    persist();
    renderSummary();
  }

  async function createAccount() {
    var email = (createEmailEl.value || "").trim();
    var password = createPasswordEl.value || "";
    var username = sanitizeUsername(createUsernameEl.value || "");
    if (!email || !password) {
      setStatus("Create account requires email + password.", "err");
      return;
    }
    if (!username) {
      setStatus("Username is required.", "err");
      return;
    }
    setStatus("Creating account...");
    try {
      var signUp = await firebasePost("accounts:signUp", {
        email: email,
        password: password,
        returnSecureToken: true
      });
      applyAuthResponse(signUp);
      session.level_server_account_username = username;
      var update = await firebasePost("accounts:update", {
        idToken: session.idToken,
        displayName: username,
        returnSecureToken: true
      });
      applyAuthResponse(update);
      persist();
      renderSummary();
      setStatus("Account created and username set.", "ok");
    } catch (err) {
      setStatus("Create account failed: " + String(err.message || err), "err");
    }
  }

  async function signIn() {
    var email = (loginEmailEl.value || "").trim();
    var password = loginPasswordEl.value || "";
    if (!email || !password) {
      setStatus("Sign in requires email + password.", "err");
      return;
    }
    setStatus("Signing in...");
    try {
      var signInResp = await firebasePost("accounts:signInWithPassword", {
        email: email,
        password: password,
        returnSecureToken: true
      });
      applyAuthResponse(signInResp);
      if (signInResp.displayName && !session.level_server_account_username) {
        session.level_server_account_username = sanitizeUsername(signInResp.displayName);
      }
      persist();
      renderSummary();
      setStatus("Signed in.", "ok");
    } catch (err) {
      setStatus("Sign in failed: " + String(err.message || err), "err");
    }
  }

  function signOut() {
    session.email = "";
    session.localId = "";
    session.idToken = "";
    session.refreshToken = "";
    session.level_server_auth_token = "";
    persist();
    renderSummary();
    setStatus("Signed out locally.", "ok");
  }

  async function changeUsername() {
    var next = sanitizeUsername(newUsernameEl.value || "");
    if (!session.idToken) {
      setStatus("Sign in first.", "err");
      return;
    }
    if (!next) {
      setStatus("New username is empty.", "err");
      return;
    }
    setStatus("Changing username...");
    try {
      var update = await firebasePost("accounts:update", {
        idToken: session.idToken,
        displayName: next,
        returnSecureToken: true
      });
      applyAuthResponse(update);
      session.level_server_account_username = next;
      persist();
      renderSummary();
      setStatus("Username changed.", "ok");
    } catch (err) {
      setStatus("Username change failed: " + String(err.message || err), "err");
    }
  }

  async function changePassword() {
    var nextPassword = newPasswordEl.value || "";
    if (!session.idToken) {
      setStatus("Sign in first.", "err");
      return;
    }
    if (!nextPassword || nextPassword.length < 6) {
      setStatus("New password must be at least 6 characters.", "err");
      return;
    }
    setStatus("Changing password...");
    try {
      var update = await firebasePost("accounts:update", {
        idToken: session.idToken,
        password: nextPassword,
        returnSecureToken: true
      });
      applyAuthResponse(update);
      persist();
      renderSummary();
      setStatus("Password changed.", "ok");
    } catch (err) {
      setStatus("Password change failed: " + String(err.message || err), "err");
    }
  }

  createAccountBtn.addEventListener("click", createAccount);
  signInBtn.addEventListener("click", signIn);
  signOutBtn.addEventListener("click", signOut);
  changeUsernameBtn.addEventListener("click", changeUsername);
  changePasswordBtn.addEventListener("click", changePassword);

  var initial = readStored();
  (async function init() {
    await loadApiDescriptorConfig();
    if (initial) {
      session = Object.assign(session, initial);
    }
    if (LEVEL_SERVER_URL) session.level_server_url = LEVEL_SERVER_URL;
    session.api_key = FIREBASE_API_KEY;
    persist();
    renderSummary();
    setStatus("Ready.");
  })();
})();
