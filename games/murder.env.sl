// Murder at Hobbs Cafe: Smallville, four days, one killer. Someone in town
// kills Isabella while she closes up the cafe. Nobody knows who -- including
// you, unless you open the Case panel and reveal it.
import smallville

environment "Murder at Hobbs Cafe" {
  behavior: "murder.behavior.sl"
  days: 4
  remove event "Valentine's Day party"
}
