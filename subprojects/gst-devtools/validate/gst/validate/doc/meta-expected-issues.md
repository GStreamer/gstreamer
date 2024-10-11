The `expected-issues` field is an array of `expected-issue` structures containing
information about issues to expect (which can be known bugs or not).

Use `gst-validate-1.0 --print-issue-types` to print information about all issue types.

For example:

``` yaml
expected-issues = {
    "expected-issue, issue-id=scenario::not-ended",
}
```
Note: Since this is [`GstStructure`](GstStructure) syntax, we need to have the structures in the
array as strings/within quotes.

**Each issue has the following fields**:

* `issue-id`: (string): Issue ID - Mandatory if `summary` is not provided.
* `summary`: (string): Summary - Mandatory if `issue-id` is not provided.
* `details`: Regex string to match the issue details `detected-on`: (string):
             The name of the element the issue happened on `level`: (string):
             Issue level
* `sometimes`: (boolean): Default: `false` -  Whether the issue happens only
               sometimes if `false` and the issue doesn't happen, an error will
               be issued.
* `issue-url`: (string): The url of the issue in the bug tracker if the issue is
               a bug.

**Warning**: This field is validate only for [`.validatetest`](gst-validate-test-file.md) files, and not `.scenario`.
