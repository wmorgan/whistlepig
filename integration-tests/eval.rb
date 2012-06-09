#!/usr/bin/ruby

require 'open3'

DIR = File.dirname(__FILE__)
CMD = "#{DIR}/../batch-run-queries #{DIR}/enron1m.index"

unless (ARGV & ["-h", "--help"]).empty?
  puts "Usage: #{$0} <corpus names>+"
  exit
end

puts "Running: #{CMD}"

num_good = num_bad = 0
Open3.popen3(CMD) do |qin, qout, qerr|
  ARGF.each do |l|
    l =~ /(\d+) (.*)$/ or raise "couldn't parse line #{ARGF.lineno}: #{l.inspect}"
    num, q = $1.to_i, $2
    qin.puts q
    while true
      result = qout.gets
      result =~ /found (\d+) results in ([\d\.]+)ms/ and break
      puts ";; warning: couldn't parse program output: #{result.inspect}"
    end
    num_found, time = $1.to_i, $2.to_f
    if num_found != num
      puts "ERROR: expected #{num}, got #{num_found} for query #{q.inspect}"
      num_bad += 1
    else
      num_good += 1
    end
  end
end

puts "#{num_good} successes, #{num_bad} failures"
if num_bad == 0
  puts "SUCCESS"
else
  puts "FAILURE"
  exit(-1)
end
