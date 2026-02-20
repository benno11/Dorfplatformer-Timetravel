(function () {
  "use strict";

  function q(id) {
    return document.getElementById(id);
  }

  var baseUrlInput = q("baseUrl");
  var authTokenInput = q("authToken");
  var accountUsernameInput = q("accountUsername");
  var levelIdInput = q("levelId");
  var levelNameInput = q("levelName");
  var authorInput = q("author");
  var levelDataInput = q("levelData");
  var output = q("output");
  var uploadBtn = q("uploadBtn");
  var listBtn = q("listBtn");
  var changeUsernameBtn = q("changeUsernameBtn");
  var ACCOUNT_STORAGE_KEY = "dfnew_account_settings_v2";

  function log(msg, cssClass) {
    var line = "[" + new Date().toISOString() + "] " + msg;
    if (cssClass) {
      output.innerHTML += '<span class="' + cssClass + '">' + escapeHtml(line) + "</span>\n";
    } else {
      output.textContent += line + "\n";
    }
    output.scrollTop = output.scrollHeight;
  }

  function escapeHtml(s) {
    return s
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function normalizeBaseUrl(url) {
    return (url || "").trim().replace(/\/+$/, "");
  }

  function authQuery(token) {
    var t = (token || "").trim();
    if (!t) return "";
    return "?auth=" + encodeURIComponent(t);
  }

  var apiConfigPromise = null;

  function loadApiConfig() {
    if (!apiConfigPromise) {
      apiConfigPromise = fetch("api.json", { method: "GET", cache: "no-store" })
        .then(function (res) {
          if (!res.ok) throw new Error("api.json HTTP " + res.status);
          return res.json();
        })
        .catch(function () {
          return {};
        });
    }
    return apiConfigPromise;
  }

  async function resolveOwnerFromToken(token) {
    var t = (token || "").trim();
    if (!t) return "";
    var cfg = await loadApiConfig();
    var apiKey = (((cfg || {}).firebase || {}).api_key || "").trim();
    if (!apiKey) {
      log("Token owner lookup skipped (missing firebase.api_key in api.json).", "err");
      return "";
    }
    var url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=" + encodeURIComponent(apiKey);
    try {
      var res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ idToken: t })
      });
      var text = await res.text();
      if (!res.ok) {
        log("Token owner lookup failed: HTTP " + res.status + " " + text, "err");
        return "";
      }
      var json = {};
      try {
        json = text ? JSON.parse(text) : {};
      } catch (e) {
        log("Token owner lookup parse error: " + String(e), "err");
        return "";
      }
      var user = (json.users && json.users[0]) || {};
      var displayName = sanitizePart(String(user.displayName || ""), "");
      if (displayName) return displayName;
      var email = String(user.email || "").trim();
      if (email) {
        var local = email.split("@")[0];
        return sanitizePart(local, "");
      }
    } catch (e) {
      log("Token owner lookup error: " + String(e), "err");
    }
    return "";
  }

  function sanitizePart(raw, fallbackValue) {
    var v = (raw || "").trim().replace(/[^a-zA-Z0-9_-]/g, "_");
    if (!v) return fallbackValue || "level";
    return v;
  }

  function makeLevelId(username, levelName) {
    var userPart = sanitizePart(username, "user");
    var levelPart = sanitizePart(levelName, "level");
    return userPart + "-" + levelPart;
  }

  function loadStoredAccountSettings() {
    try {
      var raw = localStorage.getItem(ACCOUNT_STORAGE_KEY);
      if (!raw) return;
      var json = JSON.parse(raw);
      if (!json || typeof json !== "object") return;
      if (baseUrlInput && !baseUrlInput.value && json.level_server_url) {
        baseUrlInput.value = String(json.level_server_url);
      }
      if (accountUsernameInput && !accountUsernameInput.value && json.level_server_account_username) {
        accountUsernameInput.value = sanitizePart(String(json.level_server_account_username), "");
      }
      if (authTokenInput && !authTokenInput.value && json.level_server_auth_token) {
        authTokenInput.value = String(json.level_server_auth_token);
      }
    } catch (e) {}
  }

  function persistAccountSettings() {
    try {
      var raw = localStorage.getItem(ACCOUNT_STORAGE_KEY);
      var json = raw ? JSON.parse(raw) : {};
      if (!json || typeof json !== "object") json = {};
      json.level_server_url = normalizeBaseUrl(baseUrlInput.value || "");
      json.level_server_account_username = sanitizePart((accountUsernameInput && accountUsernameInput.value) || "", "");
      json.level_server_auth_token = (authTokenInput.value || "").trim();
      localStorage.setItem(ACCOUNT_STORAGE_KEY, JSON.stringify(json));
    } catch (e) {}
  }

  function changeUsername() {
    var current = ((accountUsernameInput && accountUsernameInput.value) || "").trim();
    var next = window.prompt("Enter new username", current);
    if (next === null) return;
    next = sanitizePart(next, "");
    if (!next) {
      log("Username cannot be empty.", "err");
      return;
    }
    if (accountUsernameInput) accountUsernameInput.value = next;
    persistAccountSettings();
    log("Username changed to '" + next + "'.", "ok");
  }

  async function uploadLevel() {
    var base = normalizeBaseUrl(baseUrlInput.value);
    var token = authTokenInput.value;
    var accountUsername = ((accountUsernameInput && accountUsernameInput.value) || "").trim();
    var fallbackAuthor = ((authorInput && authorInput.value) || "").trim();
    var resolvedOwner = await resolveOwnerFromToken(token);
    var ownerName = (resolvedOwner || accountUsername || fallbackAuthor || "").trim();
    var levelNameRaw = (levelNameInput.value || levelIdInput.value || "").trim();
    var levelId = makeLevelId(ownerName, levelNameRaw);
    var levelName = sanitizePart(levelNameRaw, "level");
    var owner = sanitizePart(ownerName, "");
    var data = levelDataInput.value || "";

    if (!base) {
      log("Base URL is required.", "err");
      return;
    }
    if (!owner) {
      log("Account username is required.", "err");
      return;
    }
    if (!data.trim()) {
      log("Level data is empty.", "err");
      return;
    }

    var url = base + "/levels/" + encodeURIComponent(levelId) + ".json" + authQuery(token);
    var nowSeconds = Math.floor(Date.now() / 1000);
    var payload = {
      name: levelName,
      owner: owner,
      level_id: levelId,
      data: data,
      uploaded_at: nowSeconds,
      source: "df-new-gh-pages-uploader"
    };

    log("Uploading level '" + levelId + "' to " + url);
    try {
      var res = await fetch(url, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      var txt = await res.text();
      if (!res.ok) {
        log("Upload failed: HTTP " + res.status + " " + txt, "err");
        return;
      }
      log("Upload succeeded. Response: " + txt, "ok");
      if (resolvedOwner && accountUsernameInput) {
        accountUsernameInput.value = resolvedOwner;
        persistAccountSettings();
      }
      levelIdInput.value = levelId;
    } catch (err) {
      log("Upload error: " + String(err), "err");
    }
  }

  async function listLevels() {
    var base = normalizeBaseUrl(baseUrlInput.value);
    var token = authTokenInput.value;
    var accountUsername = ((accountUsernameInput && accountUsernameInput.value) || "").trim();
    var fallbackAuthor = ((authorInput && authorInput.value) || "").trim();
    var resolvedOwner = await resolveOwnerFromToken(token);
    var ownerPrefix = sanitizePart((resolvedOwner || accountUsername || fallbackAuthor || "").trim(), "");
    if (!base) {
      log("Base URL is required.", "err");
      return;
    }
    var query = "shallow=true";
    if ((token || "").trim()) {
      query += "&auth=" + encodeURIComponent(token.trim());
    }
    var url = base + "/levels.json?" + query;
    log("Fetching level IDs from " + url);
    try {
      var res = await fetch(url, { method: "GET" });
      var text = await res.text();
      if (!res.ok) {
        log("List failed: HTTP " + res.status + " " + text, "err");
        return;
      }
      var json = {};
      try {
        json = text ? JSON.parse(text) : {};
      } catch (e) {
        log("List parse error: " + String(e), "err");
        return;
      }
      var ids = Object.keys(json || {}).sort();
      if (ownerPrefix) {
        ids = ids.filter(function (id) { return id.indexOf(ownerPrefix + "-") === 0; });
        log("Filtered by owner '" + ownerPrefix + "'.", "ok");
      }
      log("Level count: " + ids.length, "ok");
      log("IDs: " + (ids.length ? ids.join(", ") : "<none>"));
    } catch (err) {
      log("List error: " + String(err), "err");
    }
  }

  uploadBtn.addEventListener("click", function () {
    uploadLevel();
  });
  listBtn.addEventListener("click", function () {
    listLevels();
  });
  if (changeUsernameBtn) {
    changeUsernameBtn.addEventListener("click", function () {
      changeUsername();
    });
  }

  if (accountUsernameInput) {
    accountUsernameInput.addEventListener("change", persistAccountSettings);
  }
  baseUrlInput.addEventListener("change", persistAccountSettings);
  authTokenInput.addEventListener("change", persistAccountSettings);

  loadStoredAccountSettings();

  log("Ready. Uploads use the ID format username-levelname.");
})();
