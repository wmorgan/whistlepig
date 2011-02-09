require 'rmail'
require 'open3'
require 'digest/md5'
require "whistlepig/email/decoder"

module Whistlepig
module Email
class InvalidMessageError < StandardError; end
class Message
  def initialize rawbody
    @rawbody = rawbody
    @mime_parts = {}
  end

  WHITESPACE = /[\n\s\r\t]+/

  def parse!
    @m = RMail::Parser.read @rawbody

    ## trust me, you want to hash the fuck out of the message-id field
    @msgid = Digest::MD5.hexdigest(decode_header(validate_field(:message_id, @m.header["message-id"])))
    @from = Person.from_string decode_header(validate_field(:from, @m.header["from"]))
    @date = begin
      Time.parse(validate_field(:date, @m.header["date"])).to_i
    rescue ArgumentError
      #puts "warning: invalid date field #{@m.header['date']}"
      Time.at 0
    end
    @to = Person.many_from_string decode_header(@m.header["to"]) if @m.header["to"]
    @cc = @m.header["cc"] ? Person.many_from_string(decode_header(@m.header["cc"])) : []
    @bcc = @m.header["bcc"] ? Person.many_from_string(decode_header(@m.header["bcc"])) : []
    @subject = @m.header["subject"] ? decode_header(@m.header["subject"]) : ""

    self
  end

  attr_reader :msgid, :from, :to, :cc, :bcc, :subject, :date

  def recipients; ([to] + cc + bcc).flatten.compact end

  def indexable_text
    @indexable_text ||= begin
      v = ([from.indexable_text] +
        recipients.map { |r| r.indexable_text } +
        [subject] +
        mime_parts("text/plain").map do |type, fn, content|
          if fn
            fn
          elsif type =~ /text\//
            content
          end
        end
      ).flatten.compact.join(" ")

      v.gsub(/\s+[\W\d_]+(\s|$)/, " "). # drop funny tokens
        gsub(WHITESPACE, " ")
    end
  end

  def has_attachment?; @has_attachment ||= mime_parts("text/plain").any? { |type, fn, content| fn } end

  def mime_parts preferred_type
    @mime_parts[preferred_type] ||= decode_mime_parts @m, preferred_type
  end

private

  ## unnests all the mime stuff and returns a list of [type, filename, content]
  ## tuples.
  ##
  ## for multipart/alternative parts, will only return the subpart that matches
  ## preferred_type. if none of them, will only return the first subpart.
  def decode_mime_parts part, preferred_type
    if part.multipart?
      if mime_type_for(part) =~ /multipart\/alternative/
        target = part.body.find { |p| mime_type_for(p).index(preferred_type) } || part.body.first
        decode_mime_parts target, preferred_type
      else # decode 'em all
        part.body.map { |subpart| decode_mime_parts subpart, preferred_type }.flatten 1
      end
    else
      type = mime_type_for part
      filename = mime_filename_for part
      content = mime_content_for part if type =~ /text\// # hack -- save a little time by never decoding attachments
      [[type, filename, content]]
    end
  end

private

  def validate_field what, thing
    raise InvalidMessageError, "missing '#{what}' header" if thing.nil?
    thing = thing.to_s.strip
    raise InvalidMessageError, "blank '#{what}' header: #{thing.inspect}" if thing.empty?
    thing
  end

  def mime_type_for part
    (part.header["content-type"] || "text/plain").gsub(WHITESPACE, " ").strip
  end

  ## a filename, or nil
  def mime_filename_for part
    cd = part.header["Content-Disposition"]
    ct = part.header["Content-Type"]

    ## RFC 2183 (Content-Disposition) specifies that disposition-parms are
    ## separated by ";". So, we match everything up to " and ; (if present).
    filename = if ct && ct =~ /name="?(.*?[^\\])("|;|\z)/im # find in content-type
      $1
    elsif cd && cd =~ /filename="?(.*?[^\\])("|;|\z)/m # find in content-disposition
      $1
    end

    ## filename could be RFC2047 encoded
    decode_header(filename).chomp if filename
  end

  ## rfc2047-decode a header, convert to utf-8, and normalize whitespace
  def decode_header v
    v = if Decoder.is_rfc2047_encoded? v
      Decoder.decode_rfc2047 "utf-8", v
    else # assume it's ascii and transcode
      Decoder.transcode "utf-8", "ascii", v
    end

    v.gsub(WHITESPACE, " ").strip
  end

  ## the content of a mime part itself. if the content-type is text/*,
  ## it will be converted to utf8. otherwise, it will be left in the
  ## original encoding
  def mime_content_for mime_part
    return "" unless mime_part.body # sometimes this happens...

    ct = mime_type_for mime_part
    source_charset = if ct && ct =~ /charset="?(.*?)"?(;|$)/i
      $1
    else
      "US-ASCII" # guess at what it is
    end

    ret = mime_part.decode
    if ct =~ /text\//i
      Decoder.transcode "utf-8", source_charset, ret
    else
      ret # non-text, don't transcode
    end
  end

  CMD = "html2text -nometa"
  def html_to_text html
    puts "forced to decode html. running #{CMD} on #{html.size}b mime part..."
    Open3.popen3(CMD) do |inn, out, err|
      inn.print html
      inn.close
      out.read
    end
  end
end
end
end
