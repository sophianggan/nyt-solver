// Cross-origin isolation service worker for GitHub Pages.
// Based on https://github.com/gzuidhof/coi-serviceworker (MIT).
/* global self */

if (typeof window === "undefined") {
  self.addEventListener("install", () => self.skipWaiting());
  self.addEventListener("activate", (event) => {
    event.waitUntil(self.clients.claim());
  });

  self.addEventListener("fetch", (event) => {
    event.respondWith(
      (async () => {
        const response = await fetch(event.request);
        const headers = new Headers(response.headers);
        headers.set("Cross-Origin-Opener-Policy", "same-origin");
        headers.set("Cross-Origin-Embedder-Policy", "require-corp");
        return new Response(response.body, {
          status: response.status,
          statusText: response.statusText,
          headers,
        });
      })()
    );
  });
}
