(function () {
  "use strict";
  const q = id => document.getElementById(id);
  const storageKey = "dorf_custom_server_session_v1";
  let session = null;
  try { session = JSON.parse(sessionStorage.getItem(storageKey)); } catch (_) {}

  function status(message, error = false) {
    q("status").textContent = message;
    q("status").className = "status " + (error ? "err" : "ok");
  }
  function render() {
    q("createPanel").hidden = !!session;
    q("loginPanel").hidden = !!session;
    q("usernamePanel").hidden = !session;
    q("moderatorPanel").hidden = !session?.isModerator;
    const restriction = session?.uploadRestriction;
    q("moderationNotice").hidden = !restriction && !session?.warning;
    q("uploadRestriction").textContent = restriction
      ? "Uploads suspended " + (restriction.until ? "until " + new Date(restriction.until * 1000).toLocaleString() : "permanently") + ": " + restriction.reason
      : "";
    q("accountWarning").textContent = session?.warning ? "Warning: " + session.warning.reason : "";
    q("sessionSummary").textContent = session
      ? "Signed in as " + session.displayName + " · " + location.origin
      : "Sign in or create an account for this game server.";
    if (session) sessionStorage.setItem(storageKey, JSON.stringify(session));
    else sessionStorage.removeItem(storageKey);
  }
  async function request(action, body = {}, method = "POST", url = "/api/auth/" + action) {
    const response = await fetch(url, {
      method,
      headers: { "Content-Type": "application/json",
        ...(session ? { Authorization: "Bearer " + session.idToken } : {}) },
      body: method === "GET" ? undefined : JSON.stringify(body)
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error?.message || "Request failed.");
    return result;
  }
  function bind(id, action) {
    q(id).addEventListener("click", async () => {
      q(id).disabled = true;
      try { await action(); render(); }
      catch (error) { status(error.message, true); }
      finally { q(id).disabled = false; }
    });
  }
  bind("createAccountBtn", async () => {
    session = await request("register", {
      email: q("createEmail").value.trim(),
      password: q("createPassword").value,
      displayName: q("createUsername").value.trim()
    });
    q("createPassword").value = "";
    status("Account created. You can now sign in inside the game.");
  });
  bind("signInBtn", async () => {
    session = await request("login", {
      email: q("loginEmail").value.trim(), password: q("loginPassword").value
    });
    q("loginPassword").value = "";
    status("Signed in.");
  });
  bind("signOutBtn", async () => {
    await request("logout");
    session = null;
    status("Signed out.");
  });
  async function update(body) {
    session = await request("update", { ...body, currentPassword: q("currentPassword").value });
    q("currentPassword").value = "";
    q("newPassword").value = "";
    status("Account updated. Sign in again on your other devices.");
  }
  bind("changeUsernameBtn", () => update({ displayName: q("newUsername").value.trim() }));
  bind("changePasswordBtn", () => update({ password: q("newPassword").value }));
  async function loadRatingLevels() {
    const levels = await request("", {}, "GET", "/levels.json?metadata=true");
    const selected = q("ratingLevel").value;
    q("ratingLevel").replaceChildren();
    for (const [id, level] of Object.entries(levels)) {
      const option = document.createElement("option");
      option.value = id;
      option.textContent = (level.name || id) + " [" + (level.difficulty == null ? "Unrated" : level.difficulty + "/9") + "] - " + id;
      q("ratingLevel").appendChild(option);
    }
    if (Object.hasOwn(levels, selected)) q("ratingLevel").value = selected;
    status(Object.keys(levels).length ? "Levels loaded. Select a level and difficulty." : "No shared levels yet.");
  }
  bind("loadRatedLevelsBtn", loadRatingLevels);
  bind("setDifficultyBtn", async () => {
    const id = q("ratingLevel").value;
    if (!id) throw new Error("Refresh levels and select one first.");
    const difficulty = Number(q("difficultyRating").value);
    await request("", { difficulty }, "PUT", "/levels/" + encodeURIComponent(id) + "/difficulty");
    await loadRatingLevels();
    status("Difficulty set to " + difficulty + "/9.");
  });
  render();
  if (session) request("lookup").then(result => {
    Object.assign(session, result.users[0]); render();
  }).catch(error => { session = null; render(); status(error.message, true); });
})();

