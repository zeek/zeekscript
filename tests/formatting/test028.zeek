# Some comment.
#@ BEGIN-SKIP-TESTING
@if ( some_func("/some/path") > 0 )
    @load /some/path
@else
    @load packages/some-pkg
@endif
#@ END-SKIP-TESTING
