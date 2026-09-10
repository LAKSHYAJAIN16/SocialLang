// A spatial, large-population demo. Structurally very different from mafia.sl/
// trust_game.sl: no hidden roles, no elimination, no win condition beyond a round
// cap. The point of this game is to exercise the two things Mafia/TrustGame never
// needed -- a procedurally generated `world {}` and a population big enough that
// every agent calling ask() individually, every round, would be far too slow.
//
// Two tiers, expressed with nothing more than the language already had (an `if` on
// role and a plain `fn` -- no new "behavior tier" syntax was needed):
//   - 5000 Citizens just wander between locations each round: pure SocialLang code,
//     no LLM call, so their cost doesn't depend on model latency at all.
//   - 50 Journalists are the LLM tier: each round they all get asked for a headline
//     via ask_choice_all(), which fires every one of those calls concurrently
//     instead of serializing 50 sequential round trips.
sim City {
  agents: 5050

  role Citizen {
    team: "citizen"
    memory: recent(3)
    sees: none
    count: 5000
  }

  role Journalist {
    team: "press"
    memory: recent(10)
    sees: none
    count: 50
  }

  world {
    width: 200
    height: 200
    location Home   { tag: "private", count: 400 }
    location Market { tag: "public", capacity: 200, count: 20 }
    location Plaza  { tag: "public", capacity: 500, count: 5 }
  }

  fn wander(agent, choices) {
    let dest = random_choice(choices)
    move_to(agent, dest)
  }

  phase Morning {
    if round == 1 {
      spawn_agents_at(with_role("Citizen"))
      spawn_agents_at(with_role("Journalist"), "public")
    }

    let choices = locations()
    for c in with_role("Citizen") {
      wander(c, choices)
    }

    let reporters = with_role("Journalist")
    let headlines = ask_choice_all(
      reporters,
      "What's today's biggest headline in the city?",
      ["Market prices rise", "Plaza gathering grows", "Quiet day in the city"]
    )
    for r in reporters {
      broadcast(r, headlines[r])
    }
  }

  win_condition {
    if round >= 3 { return "done" }
  }

  loop {
    run Morning
    let winner = check_win()
    if winner != null { return winner }
  }
}
