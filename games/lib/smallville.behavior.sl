// How Oak Hill's residents behave: social rules, daily routines, and how
// activities break into tasks. Scopes: rules { } is the whole town,
// rules <routine> { } a group, rules "Name" { } one resident -- the most
// specific one applies.
behavior OakHill {
  rules {
    chattiness: 1
    time_between_chats: 3   // game-hours
    conversation_length: 1   // extra exchanges: 0 brief .. 3 long
    strangers_talk: true
    news_eagerness: 4
    invite_acceptance: 1
    vision: 4   // tiles
    attention: 3   // new observations per step
    reflect_after: 150   // summed importance of new memories
  }

  // daily routines: from..to (hours, or wake / sleep relative) "activity" at place
  // places: home, work, cafe, park, pub, market, store, townhall, classroom, library, office
  routine cafe_owner {
    wake+1..8     "opening the cafe for the day" at work
    8..12         "working at the counter of the cafe" at work
    12..13        "having lunch at the cafe" at work
    13..17        "working at the counter of the cafe" at work
    17..20        "serving customers at the cafe" at work
    20..21        "closing up the cafe" at work
  }
  routine student {
    9..12         "attending class at the college" at classroom
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..16        "studying at the college library" at library
    16..17        "{currently}" at library
    17..18        "taking a walk in the park" at park
  }
  routine professor {
    wake+1..9     "preparing the day's lecture" at office
    9..12         "teaching a class at the college" at classroom
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..16        "holding office hours" at office
    16..17        "grading papers" at office
  }
  routine pharmacist {
    wake+1..12    "working at the store counter" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "working at the store counter" at work
  }
  routine shopkeeper {
    wake+1..12    "working at the store counter" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "working at the store counter" at work
  }
  routine bartender {
    wake+1..12    "running errands at the market" at market
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..15        "{currently}" at home
    15..16        "preparing the pub for the evening" at work
    16..24        "tending the bar at the pub" at work
  }
  routine politician {
    wake+1..8     "reading the newspaper over breakfast" at home
    8..10         "talking to residents in the park about the campaign" at park
    10..12        "working on the campaign at the town hall" at work
    12..13        "having lunch at the cafe" at cafe
    13..15        "campaigning around the market" at market
    15..16        "campaigning at the cafe" at cafe
    16..17        "taking a walk in the park" at park
  }
  routine comedian {
    wake+1..12    "{currently}" at home
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "writing new jokes at the cafe" at cafe
    19..22        "performing at the open mic at the pub" at work
  }
  routine photographer {
    wake+1..12    "taking photographs around the park" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..16        "editing photos at the cafe" at cafe
  }
  routine landscape_painter {
    wake+1..12    "painting in the park" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..16        "{currently}" at work
  }
  routine artist {
    wake+1..12    "painting" at home
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "{currently}" at home
  }
  routine mathematician {
    wake+1..12    "researching at the college library" at library
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..16        "{currently}" at library
  }
  routine retiree {
    wake+1..10    "tending the garden at home" at home
    10..12        "taking a walk in the park" at park
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..15        "shopping for groceries at the market" at market
  }
  routine writer {
    wake+1..12    "writing" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "{currently}" at work
  }
  routine engineer {
    wake+1..12    "coding at the desk" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "{currently}" at work
  }
  routine lawyer {
    wake+1..12    "working with clients" at work
    12..13        "having lunch at the cafe" at cafe or "having lunch at home" at home
    13..17        "{currently}" at work
  }

  // fills whatever a routine leaves open
  everyday {
    wake..wake+1  "waking up and completing the morning routine" at home
    18..19        "eating dinner" at home
  }

  // still-open hours: "activity" at place weight [social]
  free_time {
    "relaxing at home" at home 0.35
    "reading at home" at home 0.25
    "taking a walk in the park" at park 0.25
    "having a drink at the pub" at pub 0.25 social
    "hanging out at the cafe" at cafe 0.3 social
  }

  // how an hour's activity breaks into tasks: "task" object minutes
  // (the first activity whose name matches wins; "{activity}" = the hour's activity)
  activity "sleep" {
    "sleeping" "bed" 60
  }
  activity "morning routine" {
    "waking up and stretching" "bed" 5
    "using the bathroom" "toilet" 5
    "taking a shower" "shower" 10
    "getting dressed" "closet" 5
    "making breakfast" "stove" 15
    "eating breakfast" "table" 20
  }
  activity "hosting" {
    "welcoming guests" "cafe customer seating" 20
    "serving drinks to guests" "behind the cafe counter" 20
    "chatting with guests" "cafe customer seating" 20
  }
  activity "party" {
    "arriving at the party" "cafe customer seating" 10
    "chatting with people at the party" "cafe customer seating" 30
    "enjoying snacks at the party" "cafe customer seating" 20
  }
  activity "opening the cafe" {
    "unlocking the cafe and turning on the lights" "cafe customer seating" 15
    "brewing the first pot of coffee" "coffee machine" 25
    "setting up the counter" "behind the cafe counter" 20
  }
  activity "closing up" {
    "wiping down the tables" "cafe customer seating" 15
    "cleaning the coffee machine" "coffee machine" 25
    "counting the register" "behind the cafe counter" 20
  }
  activity "counter of the cafe" "serving customers" {
    "greeting customers" "behind the cafe counter" 10
    "brewing coffee for customers" "coffee machine" 15
    "taking orders" "behind the cafe counter" 15
    "baking pastries" "cooking area" 10
    "cleaning the counter" "behind the cafe counter" 10
  }
  activity "store counter" {
    "helping customers" "behind the" 15
    "restocking shelves" "shelf" 20
    "ringing up purchases" "behind the" 15
    "checking inventory" "shelf" 10
  }
  activity "tending the bar" "preparing the pub" {
    "pouring drinks" "beer taps" 20
    "chatting with customers" "behind the bar counter" 20
    "cleaning glasses" "behind the bar counter" 20
  }
  activity "open mic" {
    "warming up backstage" "bar customer seating" 20
    "performing stand-up comedy" "karaoke machine" 25
    "chatting with the audience" "bar customer seating" 15
  }
  activity "attending class" {
    "listening to the lecture" "classroom student seating" 25
    "taking notes" "classroom student seating" 20
    "discussing with classmates" "classroom student seating" 15
  }
  activity "teaching" {
    "giving a lecture" "classroom podium" 30
    "writing on the blackboard" "blackboard" 15
    "answering students' questions" "classroom podium" 15
  }
  activity "grading" {
    "grading papers" "desk" 30
    "reading a book" "bookshelf" 15
    "answering emails" "desk" 15
  }
  activity "office hours" {
    "meeting with a student" "desk" 30
    "reading a book" "bookshelf" 15
    "answering emails" "desk" 15
  }
  activity "preparing the day" {
    "preparing lecture notes" "desk" 30
    "reading a book" "bookshelf" 15
    "answering emails" "desk" 15
  }
  activity "library" "studying" "research" {
    "reading at the library" "library table" 25
    "looking for books" "bookshelf" 10
    "writing notes" "library table" 25
  }
  activity "lunch at the cafe" "hanging out at the cafe" {
    "ordering food at the counter" "cafe customer seating" 10
    "eating lunch" "cafe customer seating" 30
    "having a coffee" "cafe customer seating" 20
  }
  activity "lunch" "dinner" "eating" {
    "cooking a meal" "stove" 20
    "eating" "table" 30
    "washing the dishes" "sink" 10
  }
  activity "park" "walk" {
    "walking around the park" "park garden" 20
    "sitting on a bench" "park bench" 25
    "enjoying the view" "picnic table" 15
  }
  activity "pub" "drink" {
    "ordering a drink" "bar customer seating" 10
    "having a drink" "bar customer seating" 30
    "playing pool" "pool table" 20
  }
  activity "campaign" at work {
    "talking with residents about the campaign" "notice board" 30
    "handing out campaign flyers" "desk" 20
    "listening to residents' concerns" "desk" 10
  }
  activity "campaign" {
    "talking with residents about the campaign" "bench" 30
    "handing out campaign flyers" "bench" 20
    "listening to residents' concerns" "bench" 10
  }
  activity "market" "shopping" "errands" {
    "browsing the shelves" "grocery shelf" 20
    "picking up groceries" "grocery shelf" 20
    "paying at the counter" "behind the grocery counter" 20
  }
  activity "photo" at cafe {
    "reviewing shots" "cafe customer seating" 30
    "editing photos" "cafe customer seating" 30
  }
  activity "photo" {
    "taking photographs" "park garden" 25
    "setting up the camera" "park bench" 10
    "reviewing shots" "picnic table" 25
  }
  activity "painting" "watercolor" "animation" at work {
    "sketching ideas" "park bench" 20
    "painting" "park bench" 30
    "cleaning the brushes" "picnic table" 10
  }
  activity "painting" "watercolor" "animation" {
    "sketching ideas" "desk" 20
    "painting" "desk" 30
    "cleaning the brushes" "sink" 10
  }
  activity "coding" "app" {
    "writing code" "desk" 30
    "on a video call with the team" "desk" 15
    "making coffee" "stove" 5
    "testing the app" "desk" 10
  }
  activity "garden" {
    "watering the plants" "couch" 20
    "pulling weeds" "couch" 25
    "resting on the couch" "couch" 15
  }
  activity "newspaper" {
    "reading the newspaper" "dining table" 30
    "drinking coffee" "dining table" 30
  }
  activity "reading at home" {
    "reading a book" "couch" 30
    "relaxing on the couch" "couch" 30
  }
  activity "relaxing" {
    "watching TV" "tv" 30
    "relaxing on the couch" "couch" 30
  }
  activity "writing" "jokes" "poems" "novel" "book" "thesis" at cafe {
    "writing" "cafe customer seating" 35
    "rereading the draft" "cafe customer seating" 15
    "taking a short break" "cafe customer seating" 10
  }
  activity "writing" "jokes" "poems" "novel" "book" "thesis" {
    "writing" "desk" 35
    "rereading the draft" "desk" 15
    "taking a short break" "couch" 10
  }
  activity "clients" "taxes" {
    "reviewing client documents" "desk" 30
    "meeting with a client" "meeting table" 30
  }
  activity default {
    "{activity}" "" 60
  }

  // emoji shown over residents, by keyword in what they're doing
  emoji {
    "sleep" "😴"
    "shower" "🚿"
    "bathroom" "🚽"
    "stretch" "🙆"
    "dressed" "👕"
    "party" "🎉"
    "guests" "🎉"
    "breakfast" "🍳"
    "cook" "🍳"
    "coffee" "☕"
    "brew" "☕"
    "lunch" "🍽"
    "eat" "🍽"
    "dinner" "🍽"
    "drink" "🍺"
    "pool" "🎱"
    "perform" "🎤"
    "lecture" "📚"
    "class" "📚"
    "reading" "📖"
    "book" "📖"
    "writing" "📝"
    "notes" "📝"
    "paint" "🎨"
    "sketch" "🎨"
    "code" "💻"
    "video call" "💻"
    "photo" "📷"
    "walk" "🚶"
    "bench" "🌳"
    "view" "🌳"
    "campaign" "📣"
    "flyer" "📣"
    "grocer" "🛒"
    "shelves" "🛒"
    "customer" "🙋"
    "pour" "🍺"
    "music" "🎵"
    "compos" "🎵"
    "garden" "🌱"
    "plant" "🌱"
    "tv" "📺"
    "clean" "🧹"
    "wash" "🧹"
    "newspaper" "📰"
    "client" "💼"
  }

  // how important a memory of an activity is (1-10), by keyword
  importance {
    "sleep" 1
    "idle" 1
    "party" 8
    "mayor" 8
    "election" 8
    "invite" 8
    "hosting" 8
    "convers" 5
    "chat" 5
    "talk" 5
    "shower" 1
    "bathroom" 1
    "dressed" 1
    "stretch" 1
    "dishes" 1
    "eat" 2
    "lunch" 2
    "breakfast" 2
    "dinner" 2
    "coffee" 2
    "lecture" 4
    "class" 4
    "research" 4
    "writing" 4
    "painting" 4
  }
}
