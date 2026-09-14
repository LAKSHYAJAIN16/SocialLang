import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import "@fontsource/newsreader/400.css";
import "@fontsource/newsreader/400-italic.css";
import "@fontsource/newsreader/500-italic.css";
import "@fontsource/newsreader/600.css";
import "@fontsource/ibm-plex-mono/400.css";
import "@fontsource/ibm-plex-mono/500.css";
import "@fontsource/inter/400.css";
import "@fontsource/inter/500.css";
import "@fontsource/inter/600.css";

import "./theme.css";
import "./app.css";
import App from "./App";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
