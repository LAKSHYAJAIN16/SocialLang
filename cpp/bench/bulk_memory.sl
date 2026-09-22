// Benchmark scenario, not a game: stresses prompt construction for bulk asks.
// 400 agents with generative(8) memory each accumulate a public history, then
// all get asked at once every round -- so every round builds 400 contexts,
// each scoring every visible event (recency + importance + relevance). Run
// with sl_run's --mock-context so the mock actually requests those contexts.
sim BulkMemory {
  agents: 400

  role Resident {
    team: "town"
    memory: generative(8)
    sees: none
    count: remainder
  }

  phase Day {
    let people = alive()
    let i = 0
    while i < 20 {
      broadcast(random_choice(people), "Someone said the vote at the market is suspicious and the mayor lied about the harvest.")
      i = i + 1
    }
    let answers = ask_choice_all(people, "Who do you trust most right now?", ["the mayor", "the baker", "nobody"])
  }

  win_condition {
    if round >= 10 { return "done" }
  }

  loop {
    run Day
    let w = check_win()
    if w != null { return w }
  }
}
