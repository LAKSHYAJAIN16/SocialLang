import smallvilleMafia from "./smallville_mafia.sl?raw";
import smallville from "./smallville.sl?raw";
import village from "./village.sl?raw";
import mafia from "./mafia.sl?raw";
import trustGame from "./trust_game.sl?raw";
import outbreak from "./outbreak.sl?raw";
import city from "./city.sl?raw";

export interface GameEntry {
  id: string;
  file: string;
  title: string;
  blurb: string;
  agents: string;
  source: string;
  featured?: boolean;
}

// smallville_mafia.sl and smallville.sl lead the picker -- they're this
// sandbox's reason for existing (see PRODUCT.md). The other five stay
// available since they're valid SocialLang programs too, just with less of
// the map to look at (no persona/plan/dialogue on a non-Smallville-engine
// game; several have no world{} at all, in which case the chart falls back
// to a small fixed cluster layout -- see ChartMap's non-spatial fallback).
export const GAMES: GameEntry[] = [
  {
    id: "smallville_mafia",
    file: "smallville_mafia.sl",
    title: "Smallville Mafia",
    blurb: "Mafia's hidden roles, built on top of the Smallville engine's living town.",
    agents: "6-8",
    source: smallvilleMafia,
    featured: true,
  },
  {
    id: "smallville",
    file: "smallville.sl",
    title: "Smallville",
    blurb: "Persona, planning, reacting, dialogue, reflection -- the full Generative Agents loop.",
    agents: "4",
    source: smallville,
    featured: true,
  },
  {
    id: "village",
    file: "village.sl",
    title: "Village",
    blurb: "Hidden-role deduction with a spatial world -- wolves plan in a private den.",
    agents: "6-10",
    source: village,
  },
  {
    id: "mafia",
    file: "mafia.sl",
    title: "Mafia",
    blurb: "The classic shape: night kills, day votes, no spatial world.",
    agents: "6-10",
    source: mafia,
  },
  {
    id: "trust_game",
    file: "trust_game.sl",
    title: "Trust Game",
    blurb: "No hidden roles -- just repeated cooperate/defect rounds.",
    agents: "2",
    source: trustGame,
  },
  {
    id: "outbreak",
    file: "outbreak.sl",
    title: "Outbreak",
    blurb: "Spatial contagion at scale, with an LLM tier voting on lockdown policy.",
    agents: "620",
    source: outbreak,
  },
  {
    id: "city",
    file: "city.sl",
    title: "City",
    blurb: "5,050 agents wandering a procedurally scattered city -- a scale stress test.",
    agents: "5,050",
    source: city,
  },
];

export const DEFAULT_GAME_ID = GAMES[0].id;
