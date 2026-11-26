"use strict";

const field = id => document.getElementById(id);
field("http-notice").hidden = location.protocol !== "http:";
const challenge = location.hash.slice(1);
const valid = /^[0-9a-f]{64}$/i.test(challenge);
field("submit").disabled = !valid;
if (!valid) field("status").textContent = "Open sign-in from the multiplayer lobby.";

let existing;
try {
    existing = JSON.parse(sessionStorage.getItem("dorf_custom_server_session_v1"))?.idToken;
} catch (_) {}
field("session").hidden = !valid || !existing;

async function requestProof(body, token) {
    field("submit").disabled = field("session").disabled = true;
    field("status").textContent = "Signing in…";
    try {
        const response = await fetch(location.pathname, {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
                ...(token ? { Authorization: "Bearer " + token } : {}),
            },
            body: JSON.stringify({ ...body, challenge }),
            cache: "no-store",
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error?.message || result.error);
        field("code").value = result.proof;
        field("proof").hidden = false;
        field("status").textContent = "Signed in as " + result.username;
    } catch (error) {
        field("status").textContent = error.message || "Sign-in failed.";
    } finally {
        field("submit").disabled = field("session").disabled = false;
    }
}

field("login").addEventListener("submit", event => {
    event.preventDefault();
    if (!valid) return;
    const password = field("password").value;
    field("password").value = "";
    requestProof({ email: field("email").value, password });
});
field("session").addEventListener("click", () => requestProof({}, existing));

field("copy").addEventListener("click", async () => {
    try {
        await navigator.clipboard.writeText(field("code").value);
        field("status").textContent = "Proof copied. Return to Project 3D.";
    } catch (_) {
        field("code").select();
        field("status").textContent = "Copy the selected proof to return to the game.";
    }
});
