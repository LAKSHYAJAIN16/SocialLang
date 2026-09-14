// A spatial hidden-role social-deduction game -- Mafia's shape (wolves vs. town,
// night kills, day votes) plus a world {} so location actually drives visibility:
// wolves plan in a private Den (their night chatter is whispered only to whoever is
// physically there), and the whole village gathers at the Plaza to vote by day.
sim Village {
  agents: 6..10

  memory recent(n) {
    return last(events, n)
  }

  role Wolf {
    team: "wolf"
    memory: recent(20)
    sees: teammates
    count: 2
  }

  role Seer {
    team: "town"
    memory: full_history
    sees: none
    count: 1
  }

  role Villager {
    team: "town"
    memory: full_history
    sees: none
    count: remainder
  }

  world {
    width: 40
    height: 40
    location Plaza { tag: "public", count: 1 }
    location Den   { tag: "wolf", count: 1 }
    location House { tag: "private", count: 10 }
  }

  phase Settle {
    spawn_agents_at(with_role("Villager"), "private")
    spawn_agents_at(with_role("Seer"), "private")
    spawn_agents_at(with_role("Wolf"), "private")
  }

  phase Night {
    let den = locations_by_tag("wolf")[0]
    for w in with_role("Wolf") {
      if w.alive { move_to(w, den) }
    }

    let targets = alive()
    let votes = {}
    for w in with_role("Wolf") {
      if w.alive {
        let pick = ask_choice(w, "Who should the wolves eliminate tonight?", targets)
        votes[w] = pick
        whisper(agents_at(den), w.seat + " wants to kill " + pick.seat + ".")
      }
    }
    let victim = tally(votes)

    for s in with_role("Seer") {
      if s.alive {
        let checked = ask_choice(s, "Who do you want to investigate tonight?", alive())
        remember(s, checked.seat + " is on the " + checked.team + " team.")
      }
    }

    if victim != null {
      eliminate(victim, "killed by the wolves")
    }
  }

  phase Day {
    let plaza = locations_by_tag("public")[0]
    for a in alive() {
      move_to(a, plaza)
    }

    for a in alive() {
      broadcast(a, ask(a, "It's daytime at the plaza. What do you want to say to the group?"))
    }

    let votes = {}
    for a in alive() {
      votes[a] = ask_choice(a, "Who do you want to vote to eliminate?", alive())
    }
    let accused = tally(votes)
    if accused != null {
      eliminate(accused, "voted out by the village")
    }
  }

  win_condition {
    let wolves_left = 0
    let town_left = 0
    for a in alive() {
      if a.team == "wolf" {
        wolves_left = wolves_left + 1
      } else {
        town_left = town_left + 1
      }
    }
    if wolves_left == 0 {
      return "town"
    }
    if wolves_left >= town_left {
      return "wolf"
    }
  }

  loop {
    if round == 1 {
      run Settle
    }
    run Night
    let winner = check_win()
    if winner != null {
      return winner
    }
    run Day
    winner = check_win()
    if winner != null {
      return winner
    }
  }
}
