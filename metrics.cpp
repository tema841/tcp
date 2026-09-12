#include "metrics.hpp"
#include <algorithm>
#include <sstream>
namespace monitor {
std::uint64_t Registry::begin(const std::string& dir,const std::string& file,const std::string& peer,std::uint64_t total,std::uint64_t chunk){std::lock_guard<std::mutex>l(m_);Transfer t;t.id=next_++;t.direction=dir;t.file=file;t.peer=peer;t.total=total;t.chunk=chunk;t.state="active";items_.push_back(t);if(items_.size()>100)items_.erase(items_.begin());return t.id;}
void Registry::update(std::uint64_t id,std::uint64_t done,std::uint64_t chunk,double speed,double rtt,const std::string& checksum){std::lock_guard<std::mutex>l(m_);for(auto&t:items_)if(t.id==id){t.done=done;t.chunk=chunk;t.speed=speed;t.rtt_ms=rtt;t.checksum=checksum;return;}}
void Registry::finish(std::uint64_t id,const std::string& state,const std::string& checksum){std::lock_guard<std::mutex>l(m_);for(auto&t:items_)if(t.id==id){t.state=state;t.checksum=checksum;if(state=="completed")t.done=t.total;return;}}
std::vector<Transfer> Registry::snapshot()const{std::lock_guard<std::mutex>l(m_);return items_;}
std::uint64_t Registry::total_bytes()const{std::lock_guard<std::mutex>l(m_);std::uint64_t x=0;for(auto&t:items_)x+=t.done;return x;}
std::uint64_t Registry::completed()const{std::lock_guard<std::mutex>l(m_);return std::count_if(items_.begin(),items_.end(),[](auto&t){return t.state=="completed";});}
std::string json_escape(const std::string&s){std::string o;for(char c:s){if(c=='"'||c=='\\')o+='\\';if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else o+=c;}return o;}
std::string status_json(const Registry&r){auto v=r.snapshot();std::ostringstream o;o<<"{\"completed\":"<<r.completed()<<",\"transferred\":"<<r.total_bytes()<<",\"transfers\":[";for(size_t i=0;i<v.size();++i){auto&t=v[i];if(i)o<<',';double pct=t.total?100.0*t.done/t.total:100.0;o<<"{\"id\":"<<t.id<<",\"direction\":\""<<json_escape(t.direction)<<"\",\"file\":\""<<json_escape(t.file)<<"\",\"peer\":\""<<json_escape(t.peer)<<"\",\"state\":\""<<t.state<<"\",\"total\":"<<t.total<<",\"done\":"<<t.done<<",\"percent\":"<<pct<<",\"chunk\":"<<t.chunk<<",\"speed\":"<<t.speed<<",\"rtt\":"<<t.rtt_ms<<",\"checksum\":\""<<t.checksum<<"\"}";}o<<"]}";return o.str();}
}
