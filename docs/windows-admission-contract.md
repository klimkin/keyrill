# Windows admission stability

An ordinary input scope permits one operation after fresh identity and status validation.
Each later operation collects new evidence. Native field classification supplies metadata
for that validation.

## Documented guarantees

An editing session grants text and property access and excludes competing text-service locks.
A read/write request requires a writable document. The application must preserve document
content throughout its granted lock. These rules establish the document-access contract used
by the executor. [RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession),
[RequestLock](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacp-requestlock).

Dynamic status and context/thread compartment changes have notification interfaces. The
executor requires status and all three relevant compartment subscriptions before requesting
an editing session. Each notification revokes its frame through an atomic token. Missing advice
refuses inspection. Uncertain registration or cleanup terminates the pending authority.
Every acquired cookie is surrendered once. Callback objects own their revocation token and
DLL lease through the last host reference. [ITfStatusSink](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfstatussink-onstatuschange),
[compartment notifications](https://learn.microsoft.com/en-us/windows/win32/tsf/compartments).

Thread-manager context and focus callbacks invalidate the service generation. Independent
providers retain the actually advised sink and use it when changing context. Tests compare
character reads, composition starts, character writes, and key disposition at every reached
metadata boundary. [OnPushContext](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgreventsink-onpushcontext),
[OnSetFocus](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgreventsink-onsetfocus).

## Final operation boundary

`permit_range` completes callback-capable focus, context, view, range, status, and compartment
queries before reading the ordinary scope. Its final field comparison recaptures Win32 metadata
for the known HWND. The permit retains the scope's property and interface leases and returned
allocation throughout the operation, including every foreign reference release afterward.

Character reads, composition starts, replacements, selection changes, and composition ends each
retain a named permit. Revocation after a character write stops further character reads and
produces an unknown outcome with consumed input.

## Supported host contract

Application-provided input scope enters TSF through host attributes. Attribute-change
notifications describe changes to that data. Text-edit notifications arrive after the editing
session. The current session's first character read relies on the current property and scope
values. [GetAttribute](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextowner-getattribute),
[OnAttributeChange](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextownerservices-onattributechange),
[OnAttrsChange](https://learn.microsoft.com/en-us/windows/win32/api/textstor/nf-textstor-itextstoreacpsink-onattrschange),
[OnEndEdit](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftexteditsink-onendedit).

The supported host returns an accurate property for the requested range and an accurate current
scope from its input-scope interface during the granted property access. Dynamic status,
compartment, and context changes notify the installed sinks before the pending operation resumes.
This coherence and notification-before-resume requirement is an additional supported-host
assumption inferred from the API contracts. Its timing needs qualification on each actual host.
Microsoft documents the returned values and their access requirements. [GetValue](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue),
[GetInputScopes](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nf-inputscope-itfinputscope-getinputscopes).

Unsupported provider behavior includes stale scope values, silent status or top-context mutation
that omits its notification, and arbitrary policy mutation inside the injected metadata capture.
Production capture uses Win32 metadata getters. An unobserved violation can precede the first
read. Finite repeated queries retain that limitation.

Controlled schedules cover every reached getter and release boundary for scope revocation,
plus every reached metadata boundary for notified status, compartment, and context changes.
The external silent-mutation reproducer retains counterexamples for the unsupported behaviors
above. Application qualification collects independent UIA public-document and caret observations
alongside TSF lifecycle evidence on actual application builds. The shared path's physical
application matrix remains pending.
