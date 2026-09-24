// How the town lives through a murder: Smallville's behaviors, plus the
// mystery. Residents only know what they saw; they share it in conversation
// (trusting friends more), the killer lies, and the town votes at the meeting.
import smallville

behavior "Murder at Hobbs Cafe" {
  // Everyone wants to talk about it.
  rules { news_eagerness: 8 }

  mystery "Murder at Hobbs Cafe" {
    killer: random                 // or a name, e.g. "Tom Moreno"
    victim: "Isabella Rodriguez"
    at: "Hobbs Cafe"               // she's closing up when it happens
    day: 0
    time: 20.5                     // 8:30 pm
    meeting: "Town Hall"
    meeting_hours: 18..19          // every evening after a body is found
    rounds: 3                      // meetings before the killer gets away
  }
}
