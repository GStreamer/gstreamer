The `overrides` field is an array of override structures.

At the moment, these overrides allow you to change the severity level of specific issues,
for example changing a critical issue to a warning to allow tests to pass
when encountering known issues.

Use `gst-validate-1.0 --print-issue-types` to print information about all issue types.

For example:

``` yaml
overrides = {
    [change-severity, issue-id=runtime::not-negotiated, new-severity=warning],
    [change-severity, issue-id=g-log::critical, new-severity=info],
}
```

**Each override has the following fields**:

* `issue-id`: (string): Issue ID to override - Mandatory
* `new-severity`: (string): New severity level (critical, warning, issue, ignore) - Mandatory

Currently only `change-severity` overrides are supported.

**Warning**: This field is validate only for [`.validatetest`](gst-validate-test-file.md) files, and not `.scenario`.
