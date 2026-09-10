#include <SekiroVisionAI/Replay.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sekiro::replay {
namespace {
std::vector<std::string> csv_row(const std::string& line) {
    std::vector<std::string> out;std::string field;bool quoted=false,closed=false;
    for(std::size_t i=0;i<line.size();++i) {
        const char c=line[i];
        if(quoted) {
            if(c=='"') {
                if(i+1<line.size()&&line[i+1]=='"'){field+='"';++i;}
                else {quoted=false;closed=true;}
            } else field+=c;
        } else if(c==',') {out.push_back(field);field.clear();closed=false;}
        else if(c=='"'&&field.empty()&&!closed)quoted=true;
        else if(closed)throw std::runtime_error("Invalid text after CSV quote");
        else field+=c;
    }
    if(quoted)throw std::runtime_error("Multiline/unclosed CSV fields are not supported");
    out.push_back(field);return out;
}
double number(const std::string& text) {
    std::istringstream stream(text);stream.imbue(std::locale::classic());double value{};
    if(!(stream>>value)||!std::isfinite(value)||(stream>>std::ws,!stream.eof()))
        throw std::runtime_error("Non-finite or malformed label number: "+text);
    return value;
}
bool eligible(const Label& label) {
    return label.kind=="THREAT"&&label.reviewed&&label.evidence=="OBSERVED_CONTACT";
}
double earliest(const Label& label){return label.impact_max_ms-label.lead_max_ms;}
double latest(const Label& label){return label.impact_min_ms-label.lead_min_ms;}
bool inside(double t,const Label& label){return t>=label.start_ms&&t<=label.end_ms;}
void validate(const std::vector<Label>& labels) {
    std::set<std::string> ids;
    for(const auto& label:labels) {
        if(label.id.empty()||!ids.insert(label.id).second)throw std::runtime_error("Missing or duplicate event_id");
        if(label.kind!="THREAT"&&label.kind!="NON_THREAT"&&label.kind!="UNKNOWN")
            throw std::runtime_error("Label kind must be THREAT, NON_THREAT or UNKNOWN");
        if(!std::isfinite(label.start_ms)||!std::isfinite(label.end_ms)||label.start_ms<0||label.end_ms<=label.start_ms)
            throw std::runtime_error("Invalid labeled observation interval");
        if(eligible(label)) {
            if(!std::isfinite(label.impact_min_ms)||!std::isfinite(label.impact_max_ms)||
               !std::isfinite(label.lead_min_ms)||!std::isfinite(label.lead_max_ms)||
               label.impact_min_ms<label.start_ms||label.impact_max_ms<label.impact_min_ms||
               label.impact_max_ms>label.end_ms||label.lead_min_ms<0||label.lead_max_ms<label.lead_min_ms||
               earliest(label)>latest(label)||earliest(label)<label.start_ms||latest(label)>label.end_ms)
                throw std::runtime_error("Impact evidence cannot define a feasible fully observed Dodge window: "+label.id);
        }
    }
    for(const auto& negative:labels)if(negative.kind=="NON_THREAT"&&negative.reviewed)
        for(const auto& threat:labels)if(threat.kind=="THREAT"&&
            std::max(negative.start_ms,threat.start_ms)<std::min(negative.end_ms,threat.end_ms))
                throw std::runtime_error("Reviewed NON_THREAT interval overlaps a THREAT label");
}
}

std::string json_string(const std::string& value) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:value) {
        switch(c){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default:if(c<32)out<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c)<<std::dec;else out<<char(c);}
    }
    out<<'"';return out.str();
}
std::vector<Label> read_labels(const std::filesystem::path& path) {
    std::ifstream stream(path);if(!stream)throw std::runtime_error("Cannot read acceptance labels");
    std::string line;if(!std::getline(stream,line))throw std::runtime_error("Empty acceptance labels");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    const auto header=csv_row(line);std::map<std::string,std::size_t> columns;
    for(std::size_t i=0;i<header.size();++i)if(!columns.emplace(header[i],i).second)
        throw std::runtime_error("Duplicate CSV column");
    for(const char* key:{"event_id","kind","start_ms","end_ms","impact_min_ms","impact_max_ms", "lead_min_ms","lead_max_ms","attack_class","evidence","reviewed"})
        if(!columns.contains(key))throw std::runtime_error(std::string("Missing CSV column: ")+key);
    std::vector<Label> labels;
    while(std::getline(stream,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        const auto row=csv_row(line);if(row.size()!=header.size())throw std::runtime_error("CSV row has wrong number of columns");
        auto get=[&](const char* key)->const std::string&{return row.at(columns.at(key));};
        Label label;label.id=get("event_id");label.kind=get("kind");label.attack_class=get("attack_class");
        label.evidence=get("evidence");
        if(get("reviewed")!="true"&&get("reviewed")!="false")throw std::runtime_error("reviewed must be true or false");
        label.reviewed=get("reviewed")=="true";
        label.start_ms=number(get("start_ms"));label.end_ms=number(get("end_ms"));
        if(eligible(label)) {
            label.impact_min_ms=number(get("impact_min_ms"));label.impact_max_ms=number(get("impact_max_ms"));
            label.lead_min_ms=number(get("lead_min_ms"));label.lead_max_ms=number(get("lead_max_ms"));
        }
        labels.push_back(label);
    }
    validate(labels);return labels;
}

Acceptance evaluate(const std::vector<AcceptedDodge>& actions,const std::vector<Label>& labels) {
    validate(labels);Acceptance out;out.accepted_actions=actions.size();
    double previous=-std::numeric_limits<double>::infinity();
    for(const auto& action:actions) {
        if(!std::isfinite(action.timestamp_ms)||action.timestamp_ms<0||action.timestamp_ms<previous)
            throw std::runtime_error("Accepted Dodge timestamps must be nonnegative and ordered");
        previous=action.timestamp_ms;
    }
    std::vector<int> events;
    std::vector<std::pair<double,double>> negative_intervals;
    for(std::size_t i=0;i<labels.size();++i) {
        if(eligible(labels[i]))events.push_back(static_cast<int>(i));
        else if(labels[i].kind=="THREAT")++out.excluded;
        if(labels[i].kind=="NON_THREAT"&&labels[i].reviewed)
            negative_intervals.emplace_back(labels[i].start_ms,labels[i].end_ms);
    }
    std::stable_sort(events.begin(),events.end(),[&](int a,int b){return latest(labels[a])<latest(labels[b]);});
    out.evaluable_events=events.size();
    std::sort(negative_intervals.begin(),negative_intervals.end());
    double start=-1,end=-1,total=0;
    for(auto span:negative_intervals) {
        if(start<0){start=span.first;end=span.second;}
        else if(span.first<=end)end=std::max(end,span.second);
        else {total+=end-start;start=span.first;end=span.second;}
    }
    if(start>=0)total+=end-start;
    out.negative_minutes=total/60000.0;
    // Maximum one-to-one matching of correct windows first. A later action can
    // move an earlier association, so overlapping combo windows do not inflate misses.
    std::vector<int> matched_event(events.size(),-1),matched_action(actions.size(),-1);
    std::function<bool(int,std::vector<bool>&)> augment=[&](int action,std::vector<bool>& seen) {
        for(std::size_t event=0;event<events.size();++event) {
            const auto& label=labels[events[event]];const double t=actions[action].timestamp_ms;
            if(seen[event]||t<earliest(label)||t>latest(label))continue;
            seen[event]=true;
            if(matched_event[event]<0||augment(matched_event[event],seen)) {
                matched_event[event]=action;matched_action[action]=static_cast<int>(event);return true;
            }
        }
        return false;
    };
    for(std::size_t action=0;action<actions.size();++action){std::vector<bool> seen(events.size());augment(static_cast<int>(action),seen);}
    auto record=[&](int action,int event,const char* classification,const char* reason) {
        const auto& label=labels[events[event]];
        out.outcomes.push_back({classification,label.id,reason,action,actions[action].timestamp_ms,earliest(label),latest(label)});
    };
    for(std::size_t event=0;event<events.size();++event)if(matched_event[event]>=0) {
        record(matched_event[event],static_cast<int>(event),"SUCCESS","WITHIN_OBSERVED_CONTACT_WINDOW");++out.success;
    }
    struct Pair{double distance;int action,event;};std::vector<Pair> candidates;
    for(std::size_t action=0;action<actions.size();++action)if(matched_action[action]<0)
        for(std::size_t event=0;event<events.size();++event)if(matched_event[event]<0) {
            const auto& label=labels[events[event]];const double t=actions[action].timestamp_ms;
            if(inside(t,label))candidates.push_back({std::min(std::abs(t-earliest(label)),std::abs(t-latest(label))),static_cast<int>(action),static_cast<int>(event)});
        }
    std::stable_sort(candidates.begin(),candidates.end(),[](const Pair& a,const Pair& b){return a.distance<b.distance;});
    for(const auto& pair:candidates)if(matched_action[pair.action]<0&&matched_event[pair.event]<0) {
        matched_action[pair.action]=pair.event;matched_event[pair.event]=pair.action;
        const bool early=actions[pair.action].timestamp_ms<earliest(labels[events[pair.event]]);
        record(pair.action,pair.event,early?"EARLY":"LATE","OUTSIDE_CONTACT_WINDOW_WITHIN_LABELED_ATTACK");
        if(early)++out.early;else ++out.late;
    }
    for(std::size_t event=0;event<events.size();++event)if(matched_event[event]<0) {
        const auto& label=labels[events[event]];out.outcomes.push_back({"MISSED",label.id,"NO_ASSOCIATED_SIMULATED_INPUT",-1,0,earliest(label),latest(label)});++out.missed;
    }
    for(std::size_t action=0;action<actions.size();++action)if(matched_action[action]<0) {
        const double t=actions[action].timestamp_ms;bool negative=false;const Label* duplicate=nullptr;
        for(const auto& label:labels)if(inside(t,label)) {
            if(label.kind=="NON_THREAT"&&label.reviewed)negative=true;
            if(eligible(label))duplicate=&label;
        }
        if(negative||duplicate) {
            out.outcomes.push_back({"FALSE_DODGE",duplicate?duplicate->id:"",negative?"REVIEWED_NON_THREAT":"DUPLICATE_ACTION_IN_CONSUMED_ATTACK",static_cast<int>(action),t,0,0});
            ++out.false_dodge;if(negative)++out.false_in_negative;
        } else {
            out.outcomes.push_back({"UNEVALUATED","","NO_REVIEWED_CONTACT_OR_NON_THREAT_COVERAGE",static_cast<int>(action),t,0,0});++out.unevaluated;
        }
    }
    return out;
}

void write_acceptance(const std::filesystem::path& path,const Acceptance& result) {
    std::ofstream out(path);if(!out)throw std::runtime_error("Cannot write acceptance report");
    out.imbue(std::locale::classic());out<<std::setprecision(12);
    out<<"{\n  \"contract\":\"svai-native-acceptance-v1\",\n  \"input_kind\":\"SIMULATED_INPUT_ACCEPTED\",\n"
       <<"  \"success\":"<<result.success<<",\"early\":"<<result.early<<",\"late\":"<<result.late
       <<",\"false_dodge\":"<<result.false_dodge<<",\"missed\":"<<result.missed
       <<",\"unevaluated_actions\":"<<result.unevaluated<<",\"excluded_threats\":"<<result.excluded
       <<",\"evaluable_threats\":"<<result.evaluable_events<<",\"accepted_actions\":"<<result.accepted_actions
       <<",\"reviewed_non_threat_minutes\":"<<result.negative_minutes<<",\"false_in_non_threat\":"<<result.false_in_negative;
    out<<",\"false_per_non_threat_minute\":";
    if(result.negative_minutes>0)out<<result.false_in_negative/result.negative_minutes;else out<<"null";
    out<<",\"correct_window_event_rate\":";
    if(result.evaluable_events)out<<double(result.success)/result.evaluable_events;else out<<"null";
    out<<",\"outcomes\":[\n";
    for(std::size_t i=0;i<result.outcomes.size();++i) {
        const auto& row=result.outcomes[i];if(i)out<<",\n";
        out<<"{\"classification\":"<<json_string(row.classification)<<",\"event_id\":"<<json_string(row.event_id)
           <<",\"reason\":"<<json_string(row.reason)<<",\"action_index\":"<<row.action_index
           <<",\"timestamp_ms\":"<<row.timestamp_ms<<",\"earliest_ms\":"<<row.earliest_ms<<",\"latest_ms\":"<<row.latest_ms<<'}';
    }
    out<<"\n]}\n";if(!out)throw std::runtime_error("Writing acceptance report failed");
}
}
