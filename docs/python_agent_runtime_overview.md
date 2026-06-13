# Python Multi-Agent Runtime Overview

This document summarizes the Python-side multi-agent runtime built on top of the C++ MCP Server.

It now has two Python orchestration paths:

- the custom graph-style runtime in `client/agent_runtime/`
- an official `LangChain + LangGraph + local RAG` path in `client/run_langgraph_agent.py`

## 1. Purpose

The Python runtime is responsible for:

- accepting a user task from CLI
- talking to the MCP Server over JSON-RPC
- talking to Ollama for planning, execution, and review
- retrieving local project knowledge as lightweight RAG context
- persisting task state into SQLite
- resuming unfinished tasks

The runtime is intentionally simple in architecture:

- one local user
- one task at a time
- fixed three-agent flow
- all side effects go through MCP tools

## 2. Main Files

- `client/run_multi_agent.py`
  - CLI entrypoint
- `client/agent_runtime/mcp_client.py`
  - MCP JSON-RPC client
- `client/agent_runtime/llm_client.py`
  - Ollama client and JSON response repair helpers
- `client/agent_runtime/knowledge_base.py`
  - local knowledge retrieval and chunk scoring
- `client/agent_runtime/graph_runtime.py`
  - lightweight LangGraph-style node/edge runtime
- `client/agent_runtime/agents.py`
  - `PlannerAgent`, `ExecutorAgent`, `ReviewerAgent`
- `client/agent_runtime/orchestrator.py`
  - graph-driven task orchestration
- `client/agent_runtime/state_store.py`
  - SQLite persistence layer
- `client/langgraph_runtime.py`
  - official LangGraph agent assembly with LangChain tools
- `client/run_langgraph_agent.py`
  - official LangGraph CLI entrypoint
- `client/web_chat_server.py`
  - browser chat server for the official LangGraph agent
- `client/web/chat.html`
  - local browser UI
- `config/agent.json`
  - runtime configuration

## 3. CLI Entry

`client/run_multi_agent.py` is the only user-facing Python entrypoint.

Supported modes:

- `--task "<task>"`
  - create and run a new task
- `--resume <task_id>`
  - resume an unfinished task from SQLite

Other flags:

- `--config`
  - path to `config/agent.json`
- `--stream`
  - print progress events
- `--debug`
  - print raw event payloads and raise full exceptions

Startup sequence:

1. load and validate config
2. create `StateStore`
3. create `McpClient`
4. create 3 `OllamaClient` instances
5. create `LocalKnowledgeBase` if enabled
6. create `PlannerAgent`, `ExecutorAgent`, `ReviewerAgent`
7. create `TaskOrchestrator`
8. run `run_task()` or `resume_task()`

## 4. Runtime Flow

The orchestrator lives in `client/agent_runtime/orchestrator.py`.

The current implementation is no longer a pure hand-written `while True` loop. It now uses a lightweight graph runtime with explicit nodes and transitions, inspired by LangGraph-style orchestration.

Current graph nodes:

- `route_task`
- `plan_task`
- `select_step`
- `start_step`
- `execute_step`
- `review_step`
- `finalize_task`

### 4.1 Preflight

Before any task starts, `_preflight()` does:

1. `initialize`
2. `tools/list`

This confirms the MCP Server is reachable and caches the available tool list for later prompt construction.

### 4.1.1 Graph Entry

After preflight, task execution enters the graph runtime:

1. `route_task`
   - decides whether the task needs planning or can go straight to step selection
2. `plan_task`
   - creates the plan if no steps exist yet
3. `select_step`
   - loads the latest task state from SQLite and picks the next unfinished step
4. `start_step`
   - updates counters and emits execution events
5. `execute_step`
   - runs the current step attempt
6. `review_step`
   - validates the attempt and either retries or returns to `select_step`
7. `finalize_task`
   - performs final review and exits the graph

This is the first migration step toward a full LangGraph-style orchestration model:

- explicit nodes
- explicit allowed edges
- a shared mutable state payload
- node-returned transition commands

## 4.1.2 Official LangGraph Path

In addition to the custom runtime, the project now includes an official LangGraph execution path.

That path is assembled in `client/langgraph_runtime.py` and uses:

- `ChatOllama` from `langchain-ollama`
- dynamic `StructuredTool` wrappers around MCP tools
- official `StateGraph`
- official `ToolNode`
- official `SqliteSaver`
- existing `PlannerAgent` and `ReviewerAgent` logic through a LangChain chat adapter

The state shape is intentionally small:

- `messages`
- `task`
- `knowledge_context`
- `plan`
- `current_step_index`
- `current_step`
- `completed_steps`
- `review_feedback`
- `step_retry_count`
- `step_message_start_index`
- `final_answer`

Current official graph shape:

1. `retrieve_plan`
2. `planner`
3. `select_step`
4. `retrieve_step`
5. `executor`
6. `tools`
7. `review`
8. `finalize`

Flow:

- `retrieve_plan` injects project knowledge for task-level planning
- `planner` builds a structured step plan
- `select_step` chooses the next unfinished step
- `retrieve_step` refreshes step-specific knowledge context
- `executor` either answers directly or emits tool calls with LangChain-bound MCP tools
- `tools` runs MCP-backed LangChain tools when the model emits tool calls
- `review` validates the step and decides retry vs. advance
- `finalize` produces the final answer after all steps are approved

Checkpointing uses SQLite through `langgraph-checkpoint-sqlite`.

## 4.1.3 Browser Chat Path

The project also includes a local browser chat path:

- backend: `client/web_chat_server.py`
- frontend: `client/web/chat.html`

This path:

- serves a local HTML chat page
- accepts browser messages through `/api/chat`
- invokes the official LangGraph runtime with a `thread_id`
- exposes `/api/thread` for lightweight thread-state inspection

The page is intended as a human-friendly interface for:

- chatting with the agent in a browser
- continuing a thread with the same `thread_id`
- inspecting the current LangGraph state without opening SQLite manually

### 4.2 Task Creation

For a new task:

1. create a `task_id`
2. insert a row into `tasks`
3. store the original user input in `agent_messages`
4. emit a `task_created` event

### 4.3 Planning

If the task has no saved plan:

1. set task state to `planning`
2. retrieve project knowledge snippets for the task query
3. call `PlannerAgent.plan()`
4. validate the returned JSON structure
5. save `goal`, `success_criteria`, and `steps` into SQLite
6. emit a `plan_created` event

The planner output schema is:

```json
{
  "goal": "...",
  "success_criteria": "...",
  "steps": [
    {
      "id": "step_1",
      "description": "...",
      "required_tools": ["tool_name"],
      "expected_output": "..."
    }
  ]
}
```

### 4.4 Step Execution

The orchestrator repeatedly picks the first unfinished step and runs `_execute_and_review_step()`.

Execution loop:

1. mark step as `executing`
2. increment `execution_attempts`
3. retrieve task-and-step-specific project knowledge
4. call `ExecutorAgent.next_action()`
5. if the result is `tool_call`, call MCP `tools/call`
6. persist the tool result
7. continue until executor returns `completed` or `failed`

Executor output schema:

```json
{
  "status": "tool_call",
  "summary": "...",
  "tool_name": "read_file",
  "arguments": {
    "path": "README.md"
  }
}
```

or:

```json
{
  "status": "completed",
  "summary": "...",
  "artifacts": ["..."]
}
```

or:

```json
{
  "status": "failed",
  "summary": "...",
  "artifacts": []
}
```

### 4.5 Review

After a step is marked `completed`, the orchestrator calls `ReviewerAgent.review_step()`.

Reviewer output schema:

```json
{
  "approved": true,
  "feedback": "...",
  "retry_step_id": "",
  "final_answer": ""
}
```

Behavior:

- if approved:
  - step becomes `completed`
- if rejected:
  - increment `review_retries`
  - feed reviewer feedback back into executor
  - retry the same step
- if retry limit is exceeded:
  - fail the entire task

### 4.6 Final Review

When all steps are completed:

1. call `ReviewerAgent.finalize_task()`
2. validate final result
3. write final review to SQLite
4. update `tasks.final_answer`
5. mark task as `completed`

## 5. The Three Agents

## 5.1 PlannerAgent

Responsibilities:

- break a task into a small number of executable steps
- use only tools that exist in the current MCP tool catalog
- avoid unnecessary setup and verification steps

Validation rules:

- `goal` must exist
- `success_criteria` must exist
- `steps` must be a non-empty list
- `required_tools` must reference real MCP tools
- number of steps cannot exceed configured maximum

If the LLM output is malformed, the planner calls `repair_text()` once and re-validates the repaired JSON.

## 5.2 ExecutorAgent

Responsibilities:

- work on exactly one step at a time
- decide whether to call a tool or declare the step completed
- stay inside the current step boundary
- reuse already observed outputs from earlier steps

Important guardrails:

- may only select tools listed in the current step's `required_tools`
- may not invent tool results
- if required information is missing, must call a tool or fail

The executor also receives:

- `knowledge_context`
  - retrieved project snippets relevant to the current task or step
- `prior_tool_calls`
  - tool calls made within the current step attempt
- `completed_steps_context`
  - grounded outputs from earlier completed steps
- `reviewer_feedback`
  - retry guidance after review rejection

## 5.3 ReviewerAgent

Responsibilities:

- judge whether a single step was really completed
- judge whether the full task was completed at the end

Important guardrails:

- step review must only judge the current step
- should not claim future steps are already complete
- should only approve when evidence is present in execution output

Like the planner and executor, reviewer output is validated and repaired once if needed.

## 6. Why `completed_steps_context` Exists

This is one of the most important implementation details.

Earlier versions only gave the executor the current step and the current step's tool call history. That caused later steps to hallucinate values instead of reusing real tool outputs.

Now, before each step runs, the orchestrator builds `completed_steps_context` from SQLite:

- prior step id
- prior step description
- latest summary
- latest artifacts
- prior step tool calls
- extracted tool result text

This makes later steps grounded in observed data instead of model memory.

Example use cases:

- read a file in step 1, write the exact content in step 2
- search the workspace in step 1, read matching files in step 2
- compute a value in step 1, write or compare it in step 2

## 7. MCP Client Layer

`client/agent_runtime/mcp_client.py` is a lightweight JSON-RPC client built with Python standard library only.

Responsibilities:

- `initialize()`
- `list_tools()`
- `call_tool()`
- optional SSE monitoring via `/sse/tool_calls`

Notes:

- the runtime mainly uses synchronous JSON-RPC today
- SSE support exists but is not yet central to orchestrator flow
- tool result text is flattened via `extract_text_content()`

## 8. Local Knowledge Retrieval Layer

`client/agent_runtime/knowledge_base.py` implements a lightweight local RAG layer without external dependencies.

Behavior:

- indexes configured files and directories relative to the Python working directory
- filters by allowed suffix
- chunks text into overlapping character windows
- scores chunks by token overlap and substring matches
- returns top-k snippets as structured dictionaries

Default sources come from `config/agent.json`:

- `README.md`
- `docs/`
- `config/`
- `client/agent_runtime/`

This knowledge is injected into:

- planner prompts for task decomposition
- executor prompts for step-level grounding

The retrieval results are also stored into `agent_messages` with agent name `retriever`.

## 9. Ollama Client Layer

`client/agent_runtime/llm_client.py` wraps `POST /api/chat`.

Current settings:

- `stream: false`
- `think: false`
- `temperature: 0.2`
- timeout default: `180s`
- retry count default: `1`

Two key helpers:

- `parse_json_response()`
  - strips code fences
  - extracts the first JSON object if the model adds surrounding text
- `repair_text()`
  - asks the model to return corrected JSON only

This repair path is used by planner, executor, and reviewer.

## 10. SQLite Persistence

`client/agent_runtime/state_store.py` owns the runtime database.

Default path:

- `data/agent_runtime.sqlite3`

Tables:

- `tasks`
  - task-level lifecycle, goal, success criteria, final answer, error
- `task_steps`
  - step order, status, attempt counters, latest summary, latest artifacts
- `agent_messages`
  - raw planner/executor/reviewer outputs
- `tool_calls`
  - tool arguments, result payloads, error state
- `reviews`
  - review outcomes and final answers

Knowledge retrieval traces are stored in `agent_messages`, not in a separate table.

The runtime uses SQLite for:

- crash recovery
- `--resume`
- later debugging
- grounding later steps in prior evidence

## 11. Limits and Failure Policy

The runtime enforces hard limits from config:

- `max_plan_steps`
- `max_tool_calls_per_step`
- `max_review_retries`

Failure behavior:

- planner invalid twice -> task fails
- executor exceeds tool-call budget -> step fails
- tool errors beyond self-repair allowance -> step fails
- reviewer rejects too many times -> task fails
- final review rejects -> task fails

Any unhandled exception updates `tasks.status = failed` and stores the error string.

## 12. Resume Behavior

`resume_task(task_id)` does:

1. run preflight again
2. load task and steps from SQLite
3. if already completed, return immediately
4. if already failed, raise
5. otherwise continue from the first unfinished step

The planner is skipped when a saved plan already exists.

## 13. Real Runtime Constraints

These constraints are important in practice:

- Python does not directly read or write project files for task execution
- all external actions must go through MCP tools
- Python may read configured documentation and source files for local knowledge indexing
- MCP file tools see the MCP Server working directory, not the Python process directory
- all three agents currently share the same Ollama model
- orchestration is single-task, single-user, local-first

## 14. Current Strengths

- very small implementation surface
- standard-library-based Python runtime
- good observability through SQLite and streamed CLI events
- grounded multi-step execution is better after adding `completed_steps_context`
- local project knowledge can be injected before tool use
- JSON repair path improves resilience against model formatting drift

## 15. Current Gaps

The runtime is usable, but still a v1:

- no multi-task scheduler
- no dynamic supervisor or DAG routing
- no role-specific model configuration
- no structured artifact typing beyond simple string lists
- knowledge retrieval is lexical and local, not embedding-based
- reviewer quality still depends heavily on prompt behavior
- SSE is not yet deeply integrated into orchestrator control flow

## 16. Suggested Reading Order in Code

If you want to understand the Python side quickly, read in this order:

1. `client/run_multi_agent.py`
2. `client/agent_runtime/graph_runtime.py`
3. `client/agent_runtime/orchestrator.py`
4. `client/agent_runtime/knowledge_base.py`
5. `client/agent_runtime/agents.py`
6. `client/agent_runtime/llm_client.py`
7. `client/agent_runtime/mcp_client.py`
8. `client/agent_runtime/state_store.py`
