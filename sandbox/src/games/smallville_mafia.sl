// Mafia, built on top of the Smallville engine (see games/smallville.sl) instead of
// as its own flat night/day loop (that's games/village.sl). The hidden-role layer --
// Night's secret kill, Day's accusation vote -- is thin: two phases and a
// win_condition, structurally identical to village.sl's. Everything else is the same
// Generative Agents machinery smallville.sl demonstrates, just reused as the
// substrate other social townspeople happen to be living their lives on:
//
//   - every agent (mafia included) gets a persona, plans a day, decomposes it into
//     hourly actions, wanders the town, reacts to neighbors, and generates real
//     dialogue with whoever it runs into -- exactly like smallville.sl
//   - the town's day vote isn't a single "what do you want to say" ask like
//     mafia.sl/village.sl use -- each villager's suspicion is asked against
//     whatever their generative(k) memory actually retrieved from a full day of
//     that lived-in town (dialogue transcripts, reflections, the morning's plan),
//     not a single scripted prompt
//   - the mafia's own daily life is real cover: they plan and live a day like
//     anyone else, then peel off to the Den at night to whisper a kill vote
//
// One round of the loop is one full day: everyone plans, three "hours" of ordinary
// town life happen, then Night (mafia only), then a Day accusation vote. See
// DESIGN.md's "Generative Agents architecture" section for what smallville.sl's
// builtins do; this file only adds the Mafia-specific Night/DayVote/win_condition
// on top of them.
sim SmallvilleMafia {
  agents: 6..8

  role Mafia {
    team: "mafia"
    memory: generative(10)
    sees: teammates
    count: 2
  }

  role Villager {
    team: "town"
    memory: generative(10)
    sees: none
    count: remainder
  }

  world {
    width: 30
    height: 30
    location Home   { tag: "home", capacity: 1, count: 8 }
    location Cafe   { tag: "social", capacity: 6, count: 1 }
    location Market { tag: "social", capacity: 6, count: 1 }
    location Plaza  { tag: "public", capacity: 20, count: 1 }
    location Den    { tag: "wolf", capacity: 4, count: 1 }
  }

  fn personas() {
    return [
      "Mara, a baker who wakes at dawn and loves trading gossip.",
      "Old Tomas, a retired teacher who reads on a park bench most afternoons.",
      "Priya, a young musician who practices guitar and dreams of the city.",
      "Deshawn, a shopkeeper who values routine and worries about the market's slow season.",
      "Yusuf, a quiet fisherman who keeps mostly to himself.",
      "Elena, an innkeeper who hears everything that happens in town.",
      "Farid, a traveling merchant passing through for the season.",
      "Nadia, the town's young doctor, always a little overworked.",
    ]
  }

  // Everyone gets a persona regardless of team -- the mafia's cover *is* an
  // ordinary life in this town, not a tell.
  fn settle_in() {
    let townsfolk = alive()
    let bios = personas()
    let n = count(townsfolk)
    for i in [0, 1, 2, 3, 4, 5, 6, 7] {
      if i < n {
        set_persona(townsfolk[i], bios[i])
      }
    }
    spawn_agents_at(townsfolk, "home")
  }

  fn plan_the_day(a) {
    make_plan(
      a,
      "It's the start of a new day in town. Sketch your plan for the day, from morning to evening -- ordinary life, nothing about anyone's secrets.",
      steps=5,
    )
  }

  fn head_toward_action(a, action) {
    let tag = ask_choice(
      a,
      "Given what you're about to do (" + action + "), are you headed home or somewhere social in town?",
      ["home", "social"],
    )
    let candidates = locations_by_tag(tag)
    if count(candidates) > 0 {
      move_to(a, random_choice(candidates))
    }
  }

  fn live_the_hour(a) {
    if count(a.subplan) == 0 {
      decompose_step(a, chunks=3)
    }
    let action = current_action(a)
    if action != null {
      head_toward_action(a, action)
      broadcast(a, action)
    }
  }

  // Same reacting/dialogue as smallville.sl -- this is what feeds the day vote
  // below real social texture (who was seen with whom, what they said) instead
  // of a single direct "who do you suspect?" with no grounding.
  fn meet_neighbors(a) {
    let others = nearby(a, 8)
    if count(others) == 0 {
      return
    }
    let partner = random_choice(others)
    let reaction = react(a, partner.seat + " is right here too.")
    if reaction != null {
      converse(a, partner, topic="town life", max_turns=2)
    }
  }

  phase Hour {
    for a in alive() {
      live_the_hour(a)
      meet_neighbors(a)
      maybe_reflect(a, threshold=3.0)
      advance_plan(a)
    }
  }

  // The hidden-role layer -- structurally the same shape as village.sl's Night,
  // just running on agents who spent the day actually living in this town rather
  // than existing only to vote.
  phase Night {
    let den = locations_by_tag("wolf")[0]
    for m in with_role("Mafia") {
      if m.alive {
        move_to(m, den)
      }
    }
    let targets = alive()
    let votes = {}
    for m in with_role("Mafia") {
      if m.alive {
        let pick = ask_choice(m, "Thinking back over today, who should the mafia eliminate tonight?", targets)
        votes[m] = pick
        whisper(agents_at(den), m.seat + " wants to kill " + pick.seat + ".")
      }
    }
    let victim = tally(votes)
    if victim != null {
      eliminate(victim, "killed in the night")
    }
  }

  // The town gathers at the Plaza and votes -- but unlike mafia.sl/village.sl's
  // single scripted "what do you want to say" prompt, each agent's suspicion here
  // is grounded in generative(10) memory over a whole day of real dialogue,
  // reactions, and reflections, since that's what every Hour above actually wrote
  // to the event log.
  phase DayVote {
    let plaza = locations_by_tag("public")[0]
    for a in alive() {
      move_to(a, plaza)
    }
    let votes = {}
    for a in alive() {
      votes[a] = ask_choice(
        a,
        "Thinking back over today -- who you saw, what they said, anything that felt off -- who do you suspect is secretly working against the town?",
        alive(),
      )
    }
    let accused = tally(votes)
    if accused != null {
      eliminate(accused, "voted out by the town")
    }
  }

  win_condition {
    let mafia_left = 0
    let town_left = 0
    for a in alive() {
      if a.team == "mafia" {
        mafia_left = mafia_left + 1
      } else {
        town_left = town_left + 1
      }
    }
    if mafia_left == 0 {
      return "town"
    }
    if mafia_left >= town_left {
      return "mafia"
    }
  }

  loop {
    if round == 1 {
      settle_in()
    }
    for a in alive() {
      plan_the_day(a)
    }
    for h in [1, 2, 3] {
      run Hour
    }
    run Night
    let winner = check_win()
    if winner != null {
      return winner
    }
    run DayVote
    winner = check_win()
    if winner != null {
      return winner
    }
  }
}
