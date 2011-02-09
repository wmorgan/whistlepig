require 'whistlepig'
require 'whistlepig/email'
require 'oklahoma_mixer'

module Whistlepig
module Email
class Store
  QUERY_FILTER = Whistlepig::Query.new "", "-~deleted" # always filter out deleted messages

  def initialize base_dir
    @pstore = OklahomaMixer.open "store.tch"
    #@pstore = Rufus::Tokyo::Cabinet.new File.join(base_dir, "pstore") # broken
    #@pstore = PStore.new File.join(base_dir, "pstore") # sucks
    @index = Whistlepig::Index.new File.join(base_dir, "index")
    @query = nil # we always have (at most) one active query
    reset_timers!
  end

  def close
    @index.close
    @pstore.close
  end

  attr_reader :index_time, :store_time

  def reset_timers!
    @index_time = @store_time = 0
  end

  def add_message message, offset, labels
    raise ArgumentError, "invalid offset #{offset.inspect}" unless offset && offset >= 0

    if message.has_attachment?
      labels += ["attachment"]
    else
      labels -= ["attachment"]
    end

    labels = labels.uniq.map { |x| x.to_s.downcase }

    ## make the entry
    startt = Time.now
    entry = Whistlepig::Entry.new
    entry.add_string "msgid", message.msgid
    entry.add_string "from", message.from.indexable_text.downcase
    entry.add_string "to", message.recipients.map { |x| x.indexable_text }.join(" ").downcase
    entry.add_string "subject", message.subject.downcase
    entry.add_string "date", message.date.to_s
    entry.add_string "body", message.indexable_text.downcase

    ## add it to the index
    doc_id = @index.add_entry entry
    labels.each { |l| @index.add_label doc_id, l }
    @index_time += Time.now - startt

    ## write it to the store
    startt = Time.now
    hash = {
      :doc_id => doc_id,
      :subject => message.subject,
      :date => message.date,
      :from => message.from.to_s,
      :to => message.recipients.map { |x| x.to_s },
      :has_attachment => message.has_attachment?,
      :offset => offset
    }.merge state_from_labels(labels)

    ## add it to the store
    @pstore["doc/#{doc_id}"] = Marshal.dump hash
    @store_time += Time.now - startt

    ## congrats, you have a docid!
    doc_id
  end

  def update_message_labels doc_id, labels
    hash = docinfo_for(doc_id) or raise ArgumentError, "cannot load docinfo for doc #{doc_id.inspect}"
    labels = labels.uniq.map { |x| x.to_s.downcase }

    old_labels = hash[:labels]
    (old_labels - labels).each { |l| @index.remove_label doc_id, l }
    (labels - old_labels).each { |l| @index.add_label doc_id, l }

    hash = hash.merge state_from_labels(labels)

    @pstore["doc/#{doc_id}"] = Marshal.dump hash
    doc_id
  end

  def contains_msgid? msgid
    @index.count(Query.new("", "msgid:#{msgid}")) > 0
  end

  def size; @index.size end

  def num_results
    startt = Time.now
    num = @query.nil? ? -1 : @index.count(@query)
    elapsed = Time.now - startt
    printf "# count all results %.1fms\n", 1000 * elapsed
    num
  end

  def set_query query
    @index.teardown_query @query if @query # new query, drop old one
    @query = query.and QUERY_FILTER
    @index.setup_query @query
  end

  def load_results num
    return [] unless @query

    startt = Time.now
    doc_ids = @index.run_query @query, num

    loadt = Time.now
    #results = doc_ids.map { |id| @pstore.transaction { @pstore["doc/#{id}"] } }
    results = doc_ids.map { |id| docinfo_for id }
    endt = Time.now

    printf "# search %.1fms, load %.1fms\n", 1000 * (loadt - startt), 1000 * (endt - startt)
    results
  end

  def docinfo_for doc_id
    h = Marshal.load @pstore["doc/#{doc_id}"]
    ## we need to explicitly tell ruby 1.9 that these strings are already in utf-8
    h.map { |k, v| v.force_encoding("utf-8") if v.respond_to?(:force_encoding) }
    h
  end

private

  ## any docinfo state we want to maintain as a function of labels
  def state_from_labels labels
    { :starred => labels.include?("starred"),
      :read => labels.include?("read"),
      :deleted => labels.include?("deleted"),
      :labels => labels
    }
  end

end
end
end
