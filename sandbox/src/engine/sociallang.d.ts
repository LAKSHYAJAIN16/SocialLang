// Ambient types for the hand-ported plain-JS engine (sociallang.js). Kept
// intentionally loose (constructors/functions typed as `any`) -- it's a
// direct extraction of web/index.html's engine script, not a TS-authored
// module, and the sandbox only touches its public surface through
// src/engine/useSimulation.ts, which wraps each call with the real shapes
// from ./types.ts. TS associates this file with sociallang.js by name, so
// these are plain top-level declarations, not a `declare module` wrapper.
export declare const tokenize: any;
export declare const parse: any;
export declare const LexError: any;
export declare const ParseError: any;
export declare const SLRuntimeError: any;
export declare const Interpreter: any;
export declare const Agent: any;
export declare const Location: any;
export declare const EventRec: any;
export declare const assignAgents: any;
export declare const runSource: any;
export declare const SeededRandom: any;
export declare const MockProvider: any;
export declare const setupWorldLocations: any;
