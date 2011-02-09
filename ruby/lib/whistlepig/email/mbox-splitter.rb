require 'time'

## a custom mbox splitter / from line detector. rmail has one, but it splits on
## occurrences of "From " in text lines too. we try and be a little smarter
## here and validate the format somewhat.
module Whistlepig
module Email
class MboxSplitter
  BREAK_RE = /^From \S+ .+ \d\d\d\d$/

  def initialize stream
    @stream = stream
  end

  def is_mbox_break_line? l
    l[0, 5] == "From " or return false
    l =~ BREAK_RE or return false
    return true

    ## this next check slows us down so i'm skipping it for now

    time = $1
    begin
      puts "parsing: #{time}"
      ## hack -- make Time.parse fail when trying to substitute values from Time.now
      Time.parse time, 0
      true
    rescue NoMethodError, ArgumentError
      puts "# skipping false positive From line #{l.inspect}"
      false
    end
  end

  def each_message
    message = ""
    start_offset = last_offset = @stream.tell
    @stream.each_line do |l|
      if is_mbox_break_line?(l)
        yield message, start_offset unless message.empty?
        #puts ">> line #{@stream.lineno}"
        message = ""
        start_offset = last_offset
      else
        message << l
      end
      last_offset = @stream.tell
    end

    yield message, start_offset
  end

  def message_at offset
    @stream.seek offset
    each_message { |m, offset| return m }
  end
end
end
end
