// The smallville library: the default town. Buildings, residents,
// relationships, and events. Start a town from it with `import smallville`
// and change only what's different. Its residents' behavior is
// smallville.behavior.sl.
environment Smallville {
  behavior: "smallville.behavior.sl"
  start: "Monday, February 13, 2023"
  start_hour: 6
  days: 2
  seed: 1

  // building types: the rooms, furniture, and look every building of a type starts with
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
  
  type "town hall" {
    size: large
    floor: "#CFC6B0"
    room "town hall lobby" { objects: ["bench", "notice board"] }
    room "mayor's office" { objects: ["desk"] }
    room "meeting room" { objects: ["meeting table", "podium"] }
  }
  type office {
    floor: "#B4BCC6"
    room "office" { objects: ["office desk", "office desk", "office desk", "coffee maker"] }
    room "meeting room" { objects: ["meeting table"] }
  }
  
  type park {
    objects: ["park bench", "park bench", "park garden", "picnic table"]
  }

  // buildings, laid out on the street grid in this order
  building "Hobbs Cafe" { type: cafe }
  building "Lin family's house" { type: home  bedrooms: 3 }
  building "Oak Hill College" { type: college }
  building "The Willows Market and Pharmacy" { type: market }
  building "Moreno family's house" { type: home }
  building "Johnson Park" { type: park }
  building "The Rose and Crown Pub" { type: pub }
  building "Dorm for Oak Hill College" { type: dorm }
  building "Moore family's house" { type: home }
  building "Harvey Oak Supply Store" { type: store }
  building "Isabella Rodriguez's apartment" { type: home }
  building "Town Hall" { type: "town hall" }
  building "Artist's co-living space" { type: home  bedrooms: 3 }
  building "Adam Smith's house" { type: home }
  building "Yuriko Yamamoto's house" { type: home }
  building "Tamara Taylor and Carmen Ortiz's house" { type: home }
  building "Arthur Burton's apartment" { type: home }
  building "Ryan Park's apartment" { type: home }
  building "Giorgio Rossi's apartment" { type: home }
  building "Carlos Gomez's apartment" { type: home }

  // residents
  resident "Isabella Rodriguez" {
    age: 34
    traits: "friendly, outgoing, hospitable"
    background: "Isabella Rodriguez is the owner of Hobbs Cafe who loves to make people feel welcome. She is always looking for ways to make the cafe a place where people can come to relax and enjoy themselves."
    currently: "planning a Valentine's Day party at Hobbs Cafe with her customers on February 14th from 5pm to 7pm, and inviting everyone she meets"
    routine: cafe_owner
    home: "Isabella Rodriguez's apartment"
    bedroom: "bedroom"
    work: "Hobbs Cafe"
    wakes: 6
    sleeps: 23
    sociability: 0.95
  }
  resident "Klaus Mueller" {
    age: 20
    traits: "kind, inquisitive, passionate"
    background: "Klaus Mueller is a student at Oak Hill College studying sociology. He is passionate about social justice and loves to explore different perspectives."
    currently: "working on a research paper about the effects of gentrification in low-income communities"
    routine: student
    home: "Dorm for Oak Hill College"
    bedroom: "dorm room 1"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.55
  }
  resident "Maria Lopez" {
    age: 21
    traits: "energetic, enthusiastic, inquisitive"
    background: "Maria Lopez is a student at Oak Hill College studying physics and a part-time Twitch game streamer who loves to connect with people. She has a crush on Klaus Mueller."
    currently: "studying for her physics classes and streaming games on Twitch"
    routine: student
    home: "Dorm for Oak Hill College"
    bedroom: "dorm room 2"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.75
  }
  resident "John Lin" {
    age: 45
    traits: "patient, kind, organized"
    background: "John Lin is a pharmacy shopkeeper at the Willows Market and Pharmacy who loves to help people. He lives with his wife, Mei Lin, a college professor, and son, Eddy Lin, a student who studies music theory."
    currently: "wondering who will run in the local mayor election next month"
    routine: pharmacist
    home: "Lin family's house"
    bedroom: "bedroom 1"
    work: "The Willows Market and Pharmacy"
    wakes: 7
    sleeps: 22
    sociability: 0.6
  }
  resident "Mei Lin" {
    age: 44
    traits: "compassionate, thoughtful, curious"
    background: "Mei Lin is a professor of philosophy at Oak Hill College. She is married to John Lin and is the mother of Eddy Lin."
    currently: "preparing her next lecture on ethics"
    routine: professor
    home: "Lin family's house"
    bedroom: "bedroom 1"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 22
    sociability: 0.55
  }
  resident "Eddy Lin" {
    age: 19
    traits: "curious, analytical, musical"
    background: "Eddy Lin is a student at Oak Hill College studying music theory and composition. He loves exploring different musical styles."
    currently: "composing a piece of music for his composition class"
    routine: student
    home: "Lin family's house"
    bedroom: "bedroom 2"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.5
  }
  resident "Tom Moreno" {
    age: 48
    traits: "opinionated, hardworking, loyal"
    background: "Tom Moreno runs the Harvey Oak Supply Store and cares a lot about the town's small businesses."
    currently: "thinking about the upcoming mayor election and what it means for local businesses"
    routine: shopkeeper
    home: "Moreno family's house"
    bedroom: "bedroom 1"
    work: "Harvey Oak Supply Store"
    wakes: 6
    sleeps: 22
    sociability: 0.5
  }
  resident "Jane Moreno" {
    age: 46
    traits: "warm, practical, community-minded"
    background: "Jane Moreno is Tom Moreno's wife and works part time at the Willows Market."
    currently: "planning her vegetable garden for the spring"
    routine: shopkeeper
    home: "Moreno family's house"
    bedroom: "bedroom 1"
    work: "The Willows Market and Pharmacy"
    wakes: 7
    sleeps: 22
    sociability: 0.65
  }
  resident "Sam Moore" {
    age: 65
    traits: "wise, determined, principled"
    background: "Sam Moore is a retired resident of Oak Hill who knows many of his neighbors and loves his community."
    currently: "running for mayor of Oak Hill in the upcoming local election and telling residents about his plans"
    routine: politician
    home: "Moore family's house"
    bedroom: "bedroom 1"
    work: "Town Hall"
    wakes: 6
    sleeps: 22
    sociability: 0.9
  }
  resident "Jennifer Moore" {
    age: 63
    traits: "artistic, gentle, patient"
    background: "Jennifer Moore is a watercolor painter who is married to Sam Moore."
    currently: "preparing a set of watercolor paintings for a small exhibition"
    routine: artist
    home: "Moore family's house"
    bedroom: "bedroom 1"
    wakes: 7
    sleeps: 22
    sociability: 0.45
  }
  resident "Yuriko Yamamoto" {
    age: 40
    traits: "meticulous, direct, dependable"
    background: "Yuriko Yamamoto is a tax lawyer who helps local businesses keep their books in order."
    currently: "helping her clients prepare their taxes"
    routine: lawyer
    home: "Yuriko Yamamoto's house"
    bedroom: "bedroom"
    work: "Town Hall"
    wakes: 7
    sleeps: 23
    sociability: 0.4
  }
  resident "Carlos Gomez" {
    age: 36
    traits: "expressive, sensitive, spontaneous"
    background: "Carlos Gomez is a poet who writes about everyday life in Oak Hill."
    currently: "writing a new collection of poems"
    routine: writer
    home: "Carlos Gomez's apartment"
    bedroom: "bedroom"
    work: "Hobbs Cafe"
    wakes: 8
    sleeps: 23
    sociability: 0.6
  }
  resident "Ayesha Khan" {
    age: 22
    traits: "thoughtful, analytical, bookish"
    background: "Ayesha Khan is a student at Oak Hill College writing her senior thesis on Shakespeare's plays."
    currently: "writing her thesis on the use of language in Shakespeare's plays"
    routine: student
    home: "Dorm for Oak Hill College"
    bedroom: "dorm room 3"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.45
  }
  resident "Wolfgang Schulz" {
    age: 21
    traits: "hardworking, precise, quiet"
    background: "Wolfgang Schulz is a student at Oak Hill College studying chemistry."
    currently: "preparing for his chemistry exam"
    routine: student
    home: "Dorm for Oak Hill College"
    bedroom: "dorm room 3"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.35
  }
  resident "Abigail Chen" {
    age: 25
    traits: "creative, open-minded, playful"
    background: "Abigail Chen is a digital artist and animator who lives at the artist's co-living space."
    currently: "working on an animation project for a client"
    routine: artist
    home: "Artist's co-living space"
    bedroom: "bedroom 1"
    wakes: 8
    sleeps: 23
    sociability: 0.6
  }
  resident "Francisco Lopez" {
    age: 28
    traits: "witty, charismatic, energetic"
    background: "Francisco Lopez is an actor and comedian who performs at open mic nights."
    currently: "preparing a new stand-up set"
    routine: comedian
    home: "Artist's co-living space"
    bedroom: "bedroom 2"
    work: "The Rose and Crown Pub"
    wakes: 9
    sleeps: 24
    sociability: 0.85
  }
  resident "Hailey Johnson" {
    age: 27
    traits: "imaginative, introspective, warm"
    background: "Hailey Johnson is a writer working on her first novel."
    currently: "writing a chapter of her novel"
    routine: writer
    home: "Artist's co-living space"
    bedroom: "bedroom 2"
    work: "Hobbs Cafe"
    wakes: 8
    sleeps: 23
    sociability: 0.55
  }
  resident "Rajiv Patel" {
    age: 26
    traits: "creative, calm, observant"
    background: "Rajiv Patel is a painter who loves painting landscapes."
    currently: "painting a series of scenes of Johnson Park"
    routine: landscape_painter
    home: "Artist's co-living space"
    bedroom: "bedroom 3"
    work: "Johnson Park"
    wakes: 7
    sleeps: 23
    sociability: 0.5
  }
  resident "Latoya Williams" {
    age: 29
    traits: "curious, adventurous, friendly"
    background: "Latoya Williams is a photographer documenting life in Oak Hill."
    currently: "working on a photo series about the town's residents"
    routine: photographer
    home: "Artist's co-living space"
    bedroom: "bedroom 3"
    work: "Johnson Park"
    wakes: 7
    sleeps: 23
    sociability: 0.75
  }
  resident "Arthur Burton" {
    age: 42
    traits: "friendly, easygoing, a good listener"
    background: "Arthur Burton is the bartender at The Rose and Crown Pub who knows everyone's order."
    currently: "thinking about hosting a trivia night at the pub"
    routine: bartender
    home: "Arthur Burton's apartment"
    bedroom: "bedroom"
    work: "The Rose and Crown Pub"
    wakes: 9
    sleeps: 24
    sociability: 0.8
  }
  resident "Ryan Park" {
    age: 29
    traits: "logical, ambitious, friendly"
    background: "Ryan Park is a software engineer at a small startup who works from home."
    currently: "building a new feature for his startup's app"
    routine: engineer
    home: "Ryan Park's apartment"
    bedroom: "bedroom"
    wakes: 7
    sleeps: 24
    sociability: 0.5
  }
  resident "Giorgio Rossi" {
    age: 38
    traits: "analytical, curious, sociable"
    background: "Giorgio Rossi is a mathematician who studies patterns in nature."
    currently: "researching a paper on fractals, often at the college library"
    routine: mathematician
    home: "Giorgio Rossi's apartment"
    bedroom: "bedroom"
    work: "Oak Hill College"
    wakes: 7
    sleeps: 23
    sociability: 0.6
  }
  resident "Carmen Ortiz" {
    age: 35
    traits: "cheerful, efficient, friendly"
    background: "Carmen Ortiz is a shopkeeper at Harvey Oak Supply Store."
    currently: "organizing the store's new inventory"
    routine: shopkeeper
    home: "Tamara Taylor and Carmen Ortiz's house"
    bedroom: "bedroom 2"
    work: "Harvey Oak Supply Store"
    wakes: 7
    sleeps: 22
    sociability: 0.7
  }
  resident "Adam Smith" {
    age: 60
    traits: "philosophical, thoughtful, calm"
    background: "Adam Smith is a philosopher and writer who enjoys long walks around town."
    currently: "working on a book about ethics in daily life"
    routine: writer
    home: "Adam Smith's house"
    bedroom: "bedroom"
    wakes: 7
    sleeps: 22
    sociability: 0.45
  }
  resident "Tamara Taylor" {
    age: 40
    traits: "kind, imaginative, patient"
    background: "Tamara Taylor is an author of children's books."
    currently: "writing a new children's book"
    routine: writer
    home: "Tamara Taylor and Carmen Ortiz's house"
    bedroom: "bedroom 1"
    wakes: 7
    sleeps: 22
    sociability: 0.55
  }

  // who already knows whom
  relationship "John Lin" "Mei Lin" { note: "is married to"  closeness: 9 }
  relationship "John Lin" "Eddy Lin" { note: "is family with"  closeness: 8 }
  relationship "Mei Lin" "Eddy Lin" { note: "is family with"  closeness: 8 }
  relationship "Tom Moreno" "Jane Moreno" { note: "is married to"  closeness: 9 }
  relationship "Sam Moore" "Jennifer Moore" { note: "is married to"  closeness: 9 }
  relationship "Maria Lopez" "Klaus Mueller" { note: "has a crush on"  closeness: 4 }

  // events and news residents start out knowing (and pass on)
  event "Valentine's Day party" {
    host: "Isabella Rodriguez"
    text: "Isabella Rodriguez is hosting a Valentine's Day party at Hobbs Cafe on February 14th from 5pm to 7pm."
    at: "Hobbs Cafe"
    invite: true
    day: 1   // days after the start
    hours: 17..19
    activity: "going to Isabella's Valentine's Day party at Hobbs Cafe"
  }
  news "Sam Moore is running for mayor of Oak Hill in the upcoming local election." { from: "Sam Moore" }

  // more residents (and buildings for them) on top of the ones above
  generate {
    residents: 0
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
