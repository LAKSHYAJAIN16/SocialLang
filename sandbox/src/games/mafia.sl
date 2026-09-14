sim Mafia {
  agents: 6..10

  // a custom memory pattern, written in SocialLang itself: mafia only see their
  // last 20 visible events, not the full game history
  memory recent(n) {
    return last(events, n)
  }

  role Mafia {
    team: "mafia"
    memory: recent(20)
    sees: teammates
    count: 2
  }

  role Detective {
    team: "town"
    memory: full_history
    sees: none
    count: 1
  }

  role Doctor {
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

  phase Night {
    let targets = alive()
    let votes = {}
    for m in with_role("Mafia") {
      if m.alive {
        votes[m] = ask_choice(m, "Who should the mafia eliminate tonight?", targets)
      }
    }
    let victim = tally(votes)

    let saved = null
    for d in with_role("Doctor") {
      if d.alive {
        saved = ask_choice(d, "Who do you want to protect tonight?", targets)
      }
    }

    for det in with_role("Detective") {
      if det.alive {
        let checked = ask_choice(det, "Who do you want to investigate tonight?", alive())
        remember(det, checked.seat + " is on the " + checked.team + " team.")
      }
    }

    if victim != null and victim != saved {
      eliminate(victim, "killed by the mafia")
    }
  }

  phase Day {
    for a in alive() {
      broadcast(a, ask(a, "It's daytime. What do you want to say to the group?"))
    }

    let votes = {}
    for a in alive() {
      votes[a] = ask_choice(a, "Who do you want to vote to eliminate?", alive())
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
