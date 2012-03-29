#!/usr/bin/ruby

require 'open3'
require 'trollop'

DIR = File.dirname(__FILE__)

opts = Trollop::options do
  banner <<EOS
Repeatedly runs queries against an index. Good for smoke & stress
testing, especially against a growing index.

Usage: #{$0} [options] <corpus names>+

Where [options] include:
EOS

  opt :cmd, "Command to run queries", :default => "#{DIR}/../batch-run-queries #{DIR}/enron1m.index"
  opt :times, "Times to run these queries", :default => 1000
end

puts "Running: #{opts.cmd}"

queries = ARGV.map { |v| IO.readlines(v) }.flatten.map do |q|
  q =~ /(\d+) (.*)$/ or raise "couldn't parse line #{ARGF.lineno}: #{l.inspect}"
  num, q = $1.to_i, $2
  [num, q]
end

Open3.popen3(opts.cmd) do |qin, qout, qerr|
  opts.times.times do |i|
    puts "[try #{i + 1} / #{opts.times}]"
    num_good = num_bad = 0
    queries.each do |num, q|
      qin.puts q
      while true
        result = qout.gets or break
        if result =~ /found (\d+) results in ([\d\.]+)ms/
          num_found, time = $1.to_i, $2.to_f
          break
        else
          print "; #{result}"
        end
      end
      printf "%7s found %3d, expected %3d: %s\n", (num_found == num ? "SUCCESS" : "FAILURE"), num_found, num, q
      if num_found != num
        num_bad += 1
      else
        num_good += 1
      end
    end
    puts "#{num_good} successes, #{num_bad} failures"
  end
end
