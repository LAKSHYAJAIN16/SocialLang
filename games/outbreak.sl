// A large-scale spatial demo: 620 agents, dynamics driven entirely by nearby(),
// eliminate(), and a small LLM tier making a policy call each round.
//
// "Infected" is modeled by reusing alive()/eliminate() (like mafia.sl reuses them for
// "eliminated") rather than a new per-agent field: the language doesn't yet have a way
// to attach a custom field to an agent that survives across rounds -- `loop { }` gets
// a fresh child scope every iteration, so only Agent's own built-in fields (alive,
// death_cause, x, y, location_id) persist. That's enough here: an infected/removed
// resident is just eliminate()'d, and alive()/with_role() let every later round see
// who's still healthy.
sim Outbreak {
  agents: 620

  role Resident {
    team: "resident"
    memory: recent(3)
    sees: none
    count: 500
  }

  role PatientZero {
    team: "resident"
    memory: recent(3)
    sees: none
    count: 20
  }

  role Official {
    team: "authority"
    memory: recent(10)
    sees: none
    count: 100
  }

  world {
    width: 150
    height: 150
    location Block    { tag: "residential", count: 260 }
    location CityHall { tag: "authority", count: 1 }
  }

  fn wander(agent, choices) {
    let dest = random_choice(choices)
    move_to(agent, dest)
  }

  phase Setup {
    spawn_agents_at(with_role("Resident"), "residential")
    spawn_agents_at(with_role("PatientZero"), "residential")
    spawn_agents_at(with_role("Official"), "authority")
    for p in with_role("PatientZero") {
      eliminate(p, "index case")
    }
  }

  phase Spread {
    let officials = with_role("Official")
    let policy = ask_choice_all(officials, "Should the city enact a lockdown this round?", ["Lockdown", "Stay open"])
    let lockdown_votes = 0
    for o in officials {
      if policy[o] == "Lockdown" {
        lockdown_votes = lockdown_votes + 1
      }
    }

    let radius = 6
    if lockdown_votes * 2 >= count(officials) {
      radius = 2
      broadcast("City Hall enacts a lockdown.")
    } else {
      broadcast("City Hall keeps the city open.")
    }

    let choices = locations_by_tag("residential")
    for r in with_role("Resident") {
      if r.alive {
        wander(r, choices)
      }
    }

    for a in all_agents() {
      if a.role != "Official" and not a.alive {
        let close = nearby(a, radius)
        for c in close {
          if c.alive and c.role != "Official" {
            if random_choice([true, false, false, false, false, false]) {
              eliminate(c, "infected")
            }
          }
        }
      }
    }
  }

  win_condition {
    let infected = 0
    let residents = 0
    for a in all_agents() {
      if a.role != "Official" {
        residents = residents + 1
        if not a.alive {
          infected = infected + 1
        }
      }
    }
    if round >= 6 {
      if infected * 3 >= residents {
        return "outbreak"
      }
      return "contained"
    }
  }

  loop {
    if round == 1 {
      run Setup
    }
    run Spread
    let winner = check_win()
    if winner != null {
      return winner
    }
  }
}
