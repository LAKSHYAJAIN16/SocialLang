// A full pass through the Generative Agents architecture (Park et al. 2023,
// "Generative Agents: Interactive Simulacra of Human Behavior" -- the "Smallville"
// paper), not just its memory-stream half:
//
//   - persona            -- set_persona() folds a backstory into every prompt
//   - planning            -- make_plan() sketches the day in broad strokes,
//                            decompose_step() recursively breaks the current step
//                            into a few concrete actions, on demand
//   - reacting            -- react() decides whether an observation (a neighbor
//                            showing up) interrupts the plan or gets ignored
//   - dialogue generation -- converse() runs a real multi-turn exchange between two
//                            agents' own models when a reaction leads to a meeting
//   - reflection          -- maybe_reflect() triggers once accumulated importance
//                            crosses a threshold, same as the paper, rather than on
//                            a fixed cadence
//
// four villagers, a spatial world (homes + three shared/social spots), one 12-hour
// day. See DESIGN.md's "Generative Agents architecture" section for how each of
// these maps onto the paper's mechanisms and where this still simplifies them.
sim Smallville {
  agents: 4

  role Villager {
    team: "villager"
    memory: generative(6)
    sees: none
    count: remainder
  }

  world {
    width: 30
    height: 30
    location Home   { tag: "home", capacity: 1, count: 4 }
    location Cafe   { tag: "social", capacity: 6, count: 1 }
    location Market { tag: "social", capacity: 6, count: 1 }
    location Park   { tag: "social", capacity: 10, count: 1 }
  }

  fn personas() {
    return [
      "Mara, a baker who wakes at dawn and loves trading gossip.",
      "Old Tomas, a retired teacher who reads on a park bench most afternoons.",
      "Priya, a young musician who practices guitar and dreams of the city.",
      "Deshawn, a shopkeeper who values routine and worries about the market's slow season.",
    ]
  }

  fn settle_in() {
    let villagers = alive()
    let bios = personas()
    for i in [0, 1, 2, 3] {
      set_persona(villagers[i], bios[i])
    }
    spawn_agents_at(villagers, "home")
  }

  fn plan_the_day(a) {
    make_plan(
      a,
      "It's the start of a new day in a small town. Sketch your plan for the day, from morning to evening.",
      steps=6,
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

  // The recursive decomposition itself: re-decompose only when the last fine-
  // grained chunk of the current top-level step has run out, matching the paper's
  // "decompose the next relevant piece, not the whole day up front" approach.
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

  // Reacting: an agent that finds itself near a neighbor decides whether to stay
  // on-plan or react -- and a reaction that amounts to "notice them" leads into an
  // actual generated dialogue between the two agents' own models.
  fn meet_neighbors(a) {
    let others = nearby(a, 8)
    if count(others) == 0 {
      return
    }
    let partner = random_choice(others)
    let reaction = react(a, partner.seat + " is right here too.")
    if reaction != null {
      converse(a, partner, topic="the town", max_turns=3)
    }
  }

  phase Hour {
    for a in alive() {
      if count(a.plan) == 0 {
        plan_the_day(a)
      }
      live_the_hour(a)
      meet_neighbors(a)
      maybe_reflect(a, threshold=3.0)
      advance_plan(a)
    }
  }

  win_condition {
    if round > 12 {
      return "day_complete"
    }
  }

  loop {
    if round == 1 {
      settle_in()
    }
    run Hour
    let winner = check_win()
    if winner != null {
      return winner
    }
  }
}
