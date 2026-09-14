import type { AgentSnapshot } from "../engine/types";

interface AgentPanelProps {
  agent: AgentSnapshot | null;
  hiddenRoles: boolean;
  gameDone: boolean;
}

export function AgentPanel({ agent, hiddenRoles, gameDone }: AgentPanelProps) {
  if (!agent) {
    return (
      <section className="agent-panel agent-panel--empty" aria-label="Selected agent">
        <p>Select a star on the chart to read its catalog card.</p>
      </section>
    );
  }

  const revealTeam = !hiddenRoles || !agent.alive || gameDone;
  const currentAction =
    agent.subplan.length > 0
      ? agent.subplan[agent.subplanCursor] ?? agent.subplan[0]
      : agent.plan[agent.planCursor]?.activity ?? null;

  return (
    <section className="agent-panel" aria-label={`${agent.seat} details`}>
      <header className="agent-panel__header">
        <h2>{agent.seat}</h2>
        <span
          className={`agent-panel__status${agent.alive ? "" : " agent-panel__status--dead"}`}
        >
          {agent.alive ? "alive" : agent.deathCause ?? "eliminated"}
        </span>
      </header>

      {revealTeam ? (
        <p className="agent-panel__role">
          {agent.role} · {agent.team}
        </p>
      ) : (
        <p className="agent-panel__role agent-panel__role--hidden">role hidden until revealed</p>
      )}

      {agent.persona && <p className="agent-panel__persona">{agent.persona}</p>}

      {currentAction && (
        <div className="agent-panel__now">
          <h3>Right now</h3>
          <p>{currentAction}</p>
        </div>
      )}

      {agent.plan.length > 0 && (
        <div className="agent-panel__plan">
          <h3>Plan for the day</h3>
          <ol>
            {agent.plan.map((step, i) => (
              <li
                key={i}
                className={
                  i === agent.planCursor
                    ? "is-current"
                    : i < agent.planCursor
                      ? "is-past"
                      : ""
                }
              >
                {step.time && <span className="agent-panel__plan-time">{step.time}</span>}
                {step.activity}
              </li>
            ))}
          </ol>
        </div>
      )}

      {agent.plan.length === 0 && <p className="agent-panel__no-plan">No plan yet.</p>}
    </section>
  );
}
