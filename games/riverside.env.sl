// Riverside: a bigger town, grown entirely from the generate block -- change
// `residents` to scale it. Its residents share Oak Hill's behavior file.
environment Riverside {
  behavior: "oakhill.behavior.sl"
  start: "Monday, February 13, 2023"
  start_hour: 6
  days: 1
  seed: 7

  // building types
  type home {
    floor: "#C49A6C"
    bedrooms: 2
    room "living room" { objects: ["couch", "tv", "bookshelf", "dining table"] }
    room "kitchen" { objects: ["stove", "refrigerator", "kitchen sink", "kitchen table"] }
    room "bathroom" { objects: ["toilet", "shower", "bathroom sink"] }
    bedroom { objects: ["bed", "closet", "desk"] }
  }
  type cafe {
    size: large
    floor: "#D9B38C"
    room "cafe" { objects: ["cafe customer seating", "cafe customer seating", "cafe customer seating", "piano"] }
    room "counter" { objects: ["behind the cafe counter", "coffee machine"] }
    room "cafe kitchen" { objects: ["cooking area", "refrigerator"] }
  }
  type pub {
    floor: "#8B6A47"
    room "pub" { objects: ["bar customer seating", "bar customer seating", "pool table", "karaoke machine"] }
    room "bar" { objects: ["behind the bar counter", "beer taps"] }
  }
  type store {
    floor: "#B8B8A8"
    room "supply store" { objects: ["supply store shelf", "supply store shelf", "behind the supply store counter"] }
    room "storage room" { objects: ["storage shelf"] }
  }
  type market {
    size: large
    floor: "#D8D8BC"
    room "grocery store" { objects: ["grocery shelf", "grocery shelf", "behind the grocery counter"] }
    room "pharmacy" { objects: ["pharmacy counter", "behind the pharmacy counter"] }
  }
  type college {
    size: large
    floor: "#C9B79C"
    room "hallway" { objects: ["bench"] }
    room "classroom" { objects: ["classroom student seating", "classroom student seating", "blackboard", "classroom podium"] }
    room "library" { objects: ["library table", "library table", "bookshelf"] }
    room "professor's office" { objects: ["desk", "bookshelf"] }
  }
  type dorm {
    size: large
    floor: "#A9B8C8"
    bedrooms: 3
    room "common room" { objects: ["common room sofa", "common room table", "tv"] }
    room "dorm kitchen" { objects: ["stove", "refrigerator"] }
    room "dorm bathroom" { objects: ["toilet", "shower"] }
    bedroom "dorm room" { objects: ["bed", "desk"] }
  }
  type office {
    floor: "#B4BCC6"
    room "office" { objects: ["office desk", "office desk", "office desk", "coffee maker"] }
    room "meeting room" { objects: ["meeting table"] }
  }
  type park {
    objects: ["park bench", "park bench", "park garden", "picnic table"]
  }

  // more residents (and buildings for them)
  generate {
    residents: 1000
    one_per { home: 3  cafe: 70  pub: 90  store: 90  market: 90  park: 160  office: 45  college: 400  dorm: 250 }   // one building of each type per N generated residents
    first_names: ["Ava", "Liam", "Noah", "Emma", "Olivia", "Elijah", "Mia", "Lucas", "Amelia", "Mateo", "Harper", "Leo", "Evelyn", "Ezra", "Aria", "Luca", "Nora", "Kai", "Zoe", "Omar", "Priya", "Hiro", "Lena", "Diego", "Sofia", "Ines", "Tariq", "Maya", "Jonah", "Ruth", "Felix", "Ada", "Samir", "Chloe", "Theo", "Yara", "Ivan", "Mina", "Owen", "Rosa", "Jae", "Nadia", "Ben", "Lucia", "Kofi", "Iris", "Pablo", "Anika", "Hugo", "Leila"]
    last_names: ["Garcia", "Nguyen", "Smith", "Kim", "Patel", "Okafor", "Silva", "Novak", "Haddad", "Ito", "Brown", "Rossi", "Jensen", "Cohen", "Mendes", "Ali", "Kowalski", "Tanaka", "Dubois", "Reyes", "Singh", "Larsen", "Moreau", "Ahmed", "Walker", "Ferreira", "Yilmaz", "Chen", "Murphy", "Diaz"]
    traits: ["curious, friendly, practical", "quiet, thoughtful, kind", "outgoing, witty, warm", "organized, patient, loyal", "creative, restless, cheerful", "calm, observant, generous"]
    routine student { weight: 3  works_at: college  ages: 18..23  background: "a student at the local college"  currently: "studying for upcoming exams" }
    routine cafe_owner { weight: 1  works_at: cafe  ages: 22..60  background: "a barista at a neighborhood cafe"  currently: "learning to make new coffee drinks" }
    routine shopkeeper { weight: 1.5  works_at: store  ages: 22..70  background: "a shopkeeper at a supply store"  currently: "reorganizing the store's shelves" }
    routine pharmacist { weight: 1.5  works_at: market  ages: 24..68  background: "a clerk at the market and pharmacy"  currently: "helping customers find what they need" }
    routine bartender { weight: 1  works_at: pub  ages: 22..60  background: "a bartender at a local pub"  currently: "planning a new cocktail menu" }
    routine engineer { weight: 3  works_at: office  ages: 23..65  background: "an engineer at a local office"  currently: "finishing a project for work" }
    routine lawyer { weight: 1  works_at: office  ages: 26..70  background: "a lawyer at a local office"  currently: "preparing documents for a client" }
    routine artist { weight: 1  works_at: home  ages: 20..80  background: "a painter who works from home"  currently: "painting a new series" }
    routine writer { weight: 1  works_at: home  ages: 20..80  background: "a writer who works from home"  currently: "writing a new story" }
    routine retiree { weight: 2  works_at: home  ages: 62..90  background: "a retiree who enjoys the town's parks"  currently: "tending a small garden" }
  }
}
