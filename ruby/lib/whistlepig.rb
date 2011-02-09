require "whistlepigc"

module Whistlepig
  ## A full-text index. You can add entries to it, and you can run queries
  ## against it.
  ##
  ## To add documents, create Entry objects and call add_entry. Entries
  ## represent the document before addition; add_entry will return an integer
  ## docid and the entry can be discarded at that point.
  ##
  ## To run queries, the simplest option is to call Index#search or
  ## Index#each_result_for.
  ##
  ## The more complex option is to use setup_query, run_query, and
  ## teardown_query, in that order. The advantage of this approach is that
  ## run_query can be called multiple times, and each call will return more
  ## results, allowing for query pagination.
  class Index
    ## Runs a query and yield each matching doc id. Handles the mechanics of
    ## setting up and tearing down the query.
    def each_result_for query, chunk_size=10
      setup_query query
      begin
        while true
          results = run_query query, chunk_size
          results.each { |r| yield r }
          break if results.size < chunk_size
        end
      ensure
        teardown_query query
      end
      self
    end

    ## Convenience method. Runs a query and returns up to +max_results+
    ## matching doc ids. Handles the mechanics of setting up and tearing down
    ## the query.
    def search query, max_results=nil
      setup_query query
      ret = []
      num_per_call = max_results || 100
      begin
        while true
          results = run_query query, num_per_call
          ret += results
          break if max_results || results.size < num_per_call
        end
      ensure
        teardown_query query
      end

      ret
    end
  end

  ## Represents document, before being added to the index.
  ##
  ## Entries allow you to build up a document in memory before indexing it.
  ## Once you've built it, pass it to Index#add_entry.
  class Entry
  end

  ## A generic error.
  class Error
  end

  ## A parser error.
  class ParseError
  end

  ## A query. Queries are created from strings with Query#new. If parsing the
  ## string fails, a ParseError is thrown.
  ##
  ## At the lowest level, queries are composed of space-separated terms.
  ## Matches against that term are restricted to the default field specified at
  ## parse time.
  ##
  ##   hello                  # search for "hello" in the default field
  ##
  ## Term matches can be restricted to another field by by
  ## prefixing them with the field name and ":", e.g. "subject:hello".
  ##
  ##   subject:hello          # search for "hello" in the "subject" field
  ##
  ## Multiple terms are considered conjunctive (i.e. all must match) unless the
  ## special token "OR" appears between them. The "OR" must be capitalized
  ## in this case.
  ##   word1 word2            # search for word1 and word2
  ##   word1 OR word2         # search for word1 or word2
  ##   subject:hello bob      # "hello" in the subject field and "bob" in the
  ##                          #  default field
  ##
  ## Parentheses can be used to group disjunctions, conjunctions or fields.
  ##   (word1 OR word2) word3 # "word3" and either "word1" or "word2"
  ##   field:(word1 OR word2) # "word1" or "word2" in field "field"
  ##
  ## Phrases are specified by surrounding the terms with double quotes.
  ##  "bob jones"             # documents with the phrase "bob jones"
  ##
  ## Negations can be specified with a - prefix.
  ##   -word                  # docs without "word"
  ##   -subject:(bob OR joe)  # docs with neither "bob" nor "joe" in subject
  ##
  ## Labels are specified with a ~ prefix. Labels do not have fields.
  ##   ~inbox                 # docs with the "inbox" label
  ##   -~inbox                # docs without the "inbox" label
  ##   -~inbox subject:hello  # docs with subject "hello" and without the
  ##                          # inbox label
  ##
  ## All of the above can be mixed and matched, of course.
  ##   -subject:"spam email" ~inbox (money OR cash)
  ##   ("love you" OR "hate you") -(~deleted OR ~spam)
  ##   etc...
  ##
  ## Existing query objects can also be altered programmatically, at least to
  ## a limited extent, by calling Query#and and Query#or.
  class Query
  end
end
