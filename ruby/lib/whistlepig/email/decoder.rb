require 'iconv'

module Whistlepig
module Email
class Decoder
  class << self

  def in_ruby19_hell?
    @in_ruby19_hell = "".respond_to?(:encoding) if @in_ruby19_hell.nil?
    @in_ruby19_hell
  end

  ## here it is, ladies and gentlemen: the panacea to all your ruby 1.9 string
  ## encoding woes.
  ##
  ## (incidentally, your woes are caused because the world is full of shit, and
  ## while ruby 1.8 was happy to let it slide, ruby 1.9 complains at the
  ## slightest whiff. so consider this your gasmask.)
  ##
  ## transcode a string from a purported charset to a target charset and never
  ## fail, guaranteed, at least if the target charset is something we know
  ## about. if the string is NOT in the purported charset, we will do our best
  ## to convert it to something legible, but all we really guarantee is that
  ## the result will be in the target charset. the content might be totally
  ## wrong.
  def transcode target_charset, orig_charset, text
    ## normalize some charset names as we see them in email
    charset = case orig_charset
      when /UTF[-_ ]?8/i; "utf-8"
      when /(iso[-_ ])?latin[-_ ]?1$/i; "ISO-8859-1"
      when /iso[-_ ]?8859[-_ ]?15/i; 'ISO-8859-15'
      when /unicode[-_ ]1[-_ ]1[-_ ]utf[-_]7/i; "utf-7"
      when /^euc$/i; 'EUC-JP' # XXX try them all?
      when /^(x-unknown|unknown[-_ ]?8bit|ascii[-_ ]?7[-_ ]?bit)$/i; 'ASCII'
      else orig_charset
    end

    if in_ruby19_hell?
      ret = begin
        text.dup.force_encoding(orig_charset).encode(target_charset)
      rescue EncodingError, ArgumentError => e
      end

      (ret && ret.valid_encoding?) ? ret : force_to_ascii(text).force_encoding(target_charset)
    else
      begin
        Iconv.iconv("#{target_charset}//TRANSLIT//IGNORE", orig_charset, text + " ").join[0 .. -2] # work around iconv bug with last two characters
      rescue Errno::EINVAL, Iconv::InvalidEncoding, Iconv::InvalidCharacter, Iconv::IllegalSequence => e
        #$stderr.puts "WARNING couldn't transcode text from #{orig_charset} to #{target_charset} (#{text[0 ... 20].inspect}...): got #{e.class}: #{e.message}"
        text = force_to_ascii text
        Iconv.iconv("#{target_charset}//TRANSLIT//IGNORE", "utf-8", text + " ").join[0 .. -2]
      end
    end
  end

  ## here's the last resort. take a string and manually, slowly, gently, turn
  ## it into some fucked-up thing that we at least know is ascii.
  ##
  ## the sad reality is that email messages often have the wrong content type,
  ## and then we have to do this in order to make them actually displayable.
  ##
  ## we could improve this with some encoding detection logic, but that's far
  ## beyond the scope of what i'm interested in spending my time on.
  def force_to_ascii s
    out = ""
    s.each_byte do |b|
      if (b & 128) != 0
        out << "\\x#{b.to_s 16}"
      else
        out << b.chr
      end
    end
    #out.force_encoding Encoding::UTF_8 if in_ruby19_hell? # not necessary?
    out
  end

  ## the next methods are stolen from http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-talk/101949
  ## and lightly adapted.
  #
  # $Id: rfc2047.rb,v 1.4 2003/04/18 20:55:56 sam Exp $
  #
  # An implementation of RFC 2047 decoding.
  #
  # This module depends on the iconv library by Nobuyoshi Nakada, which I've
  # heard may be distributed as a standard part of Ruby 1.8. Many thanks to him
  # for helping with building and using iconv.
  #
  # Thanks to "Josef 'Jupp' Schugt" <jupp / gmx.de> for pointing out an error with
  # stateful character sets.
  #
  # Copyright (c) Sam Roberts <sroberts / uniserve.com> 2004
  #
  # This file is distributed under the same terms as Ruby.
  RFC2047_WORD = %r{=\?([!\#$%&'*+-/0-9A-Z\\^\`a-z{|}~]+)\?([BbQq])\?([!->@-~]+)\?=}
  RFC2047_WORDSEQ = %r{(#{RFC2047_WORD.source})\s+(?=#{RFC2047_WORD.source})}

  def is_rfc2047_encoded? s; s =~ RFC2047_WORD end

  # Decodes a string, +from+, containing RFC 2047 encoded words into a target
  # character set, +target+. See iconv_open(3) for information on the
  # supported target encodings. If one of the encoded words cannot be
  # converted to the target encoding, it is left in its encoded form.
  def decode_rfc2047 target_charset, from
    return unless is_rfc2047_encoded? from

    from = from.gsub RFC2047_WORDSEQ, '\1'
    out = from.gsub RFC2047_WORD do |word|
      source_charset, encoding, text = $1, $2, $3

      # B64 or QP decode, as necessary:
      text = case encoding
        when 'b', 'B'; text.unpack('m*')[0]
        when 'q', 'Q';
          ## RFC 2047 has a variant of quoted printable where a ' ' character
          ## can be represented as an '_', rather than =32, so convert
          ## any of these that we find before doing the QP decoding.
          text.tr("_", " ").unpack('M*')[0]
      end

      transcode target_charset, source_charset, text
    end

    #out.force_encoding target_charset if in_ruby19_hell? # not necessary?
    out
  end
end
end
end
end
