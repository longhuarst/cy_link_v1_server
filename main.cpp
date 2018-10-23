
#include <iostream>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <map>
#include <time.h>
#include <stdint.h>
#include <string>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <queue>
#include <boost/circular_buffer.hpp>
#include <boost/crc.hpp>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <string>

using namespace std;
using namespace boost::posix_time;

#define CYLINK_STX_FLAG (0xfe)
#define CYLINK_TPY_DATA (0)
#define CYLINK_TPY_CONTROL (1)

typedef struct key_value{
    uint32_t timeout;
    boost::asio::ip::udp::endpoint ep;
}key_value_type;




//router
map<uint64_t, key_value_type> router_;

//注册本地路由信息
void register_router(uint64_t uid, boost::asio::ip::udp::endpoint ep)
{
    if (router_.count(uid) == 0) {

        key_value_type key_value;
        key_value.ep = ep;
        key_value.timeout = 0;
        router_.insert(pair<uint64_t, key_value_type>(uid, key_value));
        cout << "路由信息不存在，增加路由信息" << endl;
    }
}

//转发
void transpond(boost::asio::ip::udp::socket &socket_,uint8_t *buffer,int size,uint64_t uid)
{
    map<uint64_t, key_value_type>::iterator iter_;

    cout << "查找路由表" << endl;
    iter_ = router_.find(uid);


    if (iter_ != router_.end()) {
        cout << "转发目标：" << iter_->second.ep.address() << endl;

        socket_.send_to(boost::asio::buffer(buffer, size), iter_->second.ep);

        ptime now = second_clock::local_time();
        string now_str  =  to_iso_extended_string(now.date()) + " " + to_simple_string(now.time_of_day());
        cout << now_str <<"　　　转发完成."<< endl;
    }
    else {
        cout << "转发目标" << uid << "不存在" << endl;
    }
}

//异步定时器调用函数
void timeout(const boost::system::error_code &ec, boost::asio::deadline_timer* pt) {

    //cout<<"timeout"<<endl;
    //cout<<boost::this_thread::get_id()<<endl;


    for (map<uint64_t, key_value>::iterator iter = router_.begin(); iter != router_.end(); ){

        iter->second.timeout++;
        cout << "timeout = " << iter->second.timeout << endl;

        if (iter->second.timeout >= 10){
            cout<<"timeout >= 10,离线，删除路由信息"<<endl;

            router_.erase(iter++);

        }else{

            ++iter;
        }


    }

    // 3秒循环执行
    pt->expires_at(pt->expires_at() + boost::posix_time::seconds(1)) ; //expires_at将计时器的到期时间作为绝对时间。
    pt->async_wait(boost::bind(timeout, boost::asio::placeholders::error, pt)); //新的异步等待

}


//有心跳,timeout清零
void heart_beat(uint64_t suid_){

    map<uint64_t, key_value_type>::iterator iter_;


    iter_ = router_.find(suid_);


    if (iter_ != router_.end()) {
        if (iter_->second.timeout > 0){
            cout<<"检测到数据，timeout清零"<<endl;
            iter_->second.timeout = 0;
        }
    }
    else {

    }

}


boost::array<uint8_t, 65535> recv_buf_;
char recv_buf2_ [65535];
boost::circular_buffer<char> cb(65535);


boost::asio::ip::udp::endpoint remote_endpoint_;

boost::asio::ip::udp::socket *socket_;

boost::asio::serial_port *serial_port_;

//解一帧数据
void decoder(boost::array<uint8_t, 65535> recv_buf_)
{
    int length_ = recv_buf_[1] << 8 | recv_buf_[2];
    int seq_ = recv_buf_[3];
    int type_ = recv_buf_[4];
    int message_ = recv_buf_[5];
    uint64_t suid_ = (((uint64_t)recv_buf_[6]<<32) ) | (uint64_t)recv_buf_[7] << 24 | (uint64_t)recv_buf_[8] << 16 | (uint64_t)recv_buf_[9] << 8 | (uint64_t)recv_buf_[10];
    uint64_t duid_ = (((uint64_t)recv_buf_[11]<<32) ) | (uint64_t)recv_buf_[12] << 24 | (uint64_t)recv_buf_[13] << 16 | (uint64_t)recv_buf_[14] << 8 | (uint64_t)recv_buf_[15];
    for(uint8_t i=0;i<20;i++)
    {
        cout << "recv_buf_["<<i<<"] = ";

        printf("%d\r\n",recv_buf_[i]);
    }

    if (recv_buf_[0] == CYLINK_STX_FLAG) {
        cout << "获取到cylinkv1.0数据包 ["<< length_ <<"]" << endl;
        switch (type_)
        {
            case CYLINK_TPY_DATA:
                cout << "负载类型：透传帧(" << suid_ << "--->" << duid_ << ")" << endl;
                printf("%llu ---> %llu \r\n",(uint64_t)suid_, (uint64_t)duid_);


                register_router(suid_, remote_endpoint_);//注册本地路由表

                transpond(*socket_,recv_buf_.data(), length_ + 18, duid_);

                heart_beat(suid_);//心跳


                break;
            case CYLINK_TPY_CONTROL:
                cout << "负载类型：控制帧("<< suid_ <<")" << endl;

                register_router(suid_, remote_endpoint_);//注册本地路由表

                heart_beat(suid_);//心跳


                break;
            default:
                break;
        }

    }
    else {
        cout << "不符合cylink1.0数据协议" << endl;
        // cout << "recv_buf_[0] = ";// << endl;
        // printf("%02x %02x\r\n",recv_buf_[0],CYLINK_STX_FLAG);
    }

}

//udp收数据
void udp_receiver(boost::array<uint8_t, 65535> recv_buf_,const boost::system::error_code& error_code_, std::size_t size)
{
    if (!error_code_ || error_code_ != boost::asio::error::message_size)
    {
        decoder(recv_buf_);


    }


    socket_->async_receive_from(boost::asio::buffer(recv_buf_),remote_endpoint_,boost::bind(udp_receiver,recv_buf_,boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}


typedef struct {
    char *  buffer;int length;

}decoder_buffer_elemt_type;

//串口收数据
void handle_read(char *buf,boost::system::error_code ec,std::size_t bytes_transferred)
{


    cout<<"serialport read... [";
    cout.write(buf, bytes_transferred);
    cout<<"]"<<endl;

    cout << "本次收到数据总数 : " <<bytes_transferred<<endl;
    //收到的数据存进循环缓存
    for(int i= 0;i< bytes_transferred;i++)
    {

        cb.push_back(recv_buf2_[i]);
        cout<< cb[i] << ",";

    }
    cout << endl;


    //剔除前面无效数据
    for (int i=0;i<cb.size();++i){
        cout<< cb[0] << endl;
        if (cb[0] != 0xfe){
            cb.pop_front();


            //cout << to_string(cb.size()) << endl;
        }else{
            break;
        }

    }

    //扫描出所有0xfe的位置 存放在队列　Q
    queue<int> Q;
    for (int i=0;i<cb.size();++i){
        if (cb[i] == 0xfe){
            Q.push(i);
        }
    }




    queue<decoder_buffer_elemt_type *> decoder_buffer_;


    //queue<int> success,failed,need;//成功　错误 数据不够

    boost::circular_buffer<char> success(65535);
    boost::circular_buffer<char> need(65535);
    //循环队列去并行解码
    for (int i=0;i<Q.size();++i){
        int len = cb[Q.front()+1] << 8 | cb[Q.front()+2];

        decoder_buffer_elemt_type *pElemt = new decoder_buffer_elemt_type();


        //申请空间
        pElemt->buffer = new char[len+18];
        pElemt->length = len+18;

        if ((Q.front() + pElemt->length )> cb.size()){ // > ?  >= ? 自己看
            //长度不够
            need.push_back(Q.front());
        }else{
            //长度够
            //校验

            char data[len+15] ;


            for(int i=0 ; i < (len+15); i++)
            {
                data[i] = cb[Q.front()+i+1];
            }

            boost::crc_basic<16> crc_ccitt1( 0x1021, 0xFFFF, 0, false, false );
            crc_ccitt1.process_bytes( data,sizeof(data));

            int crc_ = cb[Q.front()+16] << 8 | cb[Q.front()+17];
            if (crc_ccitt1.checksum() == crc_){
                //对

                success.push_back(Q.front());











            }else{
                //WRONG

                //failed.push_back(Q.front());

            }
        }


        //
        if (need.size()>0){


            for (int i=0;i<need.size();++i){

                int current = need[i];

                for (int i=0;i<success.size();++i){
                    if (current < success[i]){
                        //这个数据不够的帧是错误的 把这个ｎｅｅｄ　删除
                        need.pop_front();//删除
                        --i;
                        break;
                    }

                }


            }

        }

        //把ｓｕｃｃｅｓｓ　给　ｕｄｐ＿ｒｅｃｖｅｒ　解码

        //处理剩余数据


        //正确的数据,丢给decoder解码
        boost::array<uint8_t,65535> succ_buf;
        for(int i=0;i< (len+18);i++)
        {
            succ_buf[i] = cb[success[0]+i];

        }

        decoder(succ_buf);

        if(need.size() >0 ){

            //找第一个need
            //need[0];是在之前的循环缓存中的游标   把 need[0] 之前的数据全部删掉  循环缓冲
            for(int i= 0;i<need[0];i++)
            {
                cb.pop_front();

            }



        }else{
            //找最后一个success

            //success[0] + 1 +2  == len + success[0]

        }




        //char data[len+18];

        // for(int i=0 ; i < sizeof(data); i++)
        // {
        // 	data[i] = cb[Q.front()+i];
        // }

        //  pElemt.buffer = data;
        //  pElemt.length = sizeof(data);

        // decoder_buffer_.push(pElemt);

        Q.pop();


    }



    serial_port_->async_read_some(boost::asio::buffer(recv_buf2_),boost::bind(handle_read, recv_buf2_, _1, _2));



}






int main(int argc,char *argv[])
{
//异步定时器，异步udp，异步串口

    cout << "CYLINK 服务端" << endl;

    //io_service类代表了系统里的异步处理机制，它的成员函数run()启动事件循环．阻塞等待所有注册到io_service的事件完成
    boost::asio::io_service io_service_;

    //deadline_timer需要包含库 <boost/date_time/posix_time/posix_time.hpp>
    boost::asio::deadline_timer t(io_service_, boost::posix_time::seconds(1)); //定时器．从现在开始，１秒后终止

    t.async_wait(boost::bind(timeout, boost::asio::placeholders::error, &t));
    cout<<"to run"<<endl;

    serial_port_ = new boost::asio::serial_port (io_service_,"/dev/ttyUSB0");//创建串口对象
    serial_port_->set_option(boost::asio::serial_port::baud_rate(115200));//波特率
    serial_port_->set_option(boost::asio::serial_port::flow_control(boost::asio::serial_port::flow_control::none));//流量控制
    serial_port_->set_option(boost::asio::serial_port::parity(boost::asio::serial_port::parity::none));//奇偶校验
    serial_port_->set_option(boost::asio::serial_port::stop_bits(boost::asio::serial_port::stop_bits::one));//停止位
    serial_port_->set_option(boost::asio::serial_port::character_size(8));//数据位

    // Byte[] BSendTemp = new Byte[1]; //建立临时字节数组对象
    // BSendTemp[0]=Byte.Parse(this.richTextBox_serOutPut.Text);//由文本框读入想要发送的数据


    //uint8_t buffer[12] = {0xfe,0x00.0x05};
    boost::array<uint8_t, 10> buffer;
    buffer[0]=0x00;
    buffer[1]=0xfe;
    buffer[2]=0x05;
    buffer[3]=0x00;
    buffer[4]=0x00;
    buffer[5]=0x00;
    buffer[6]=0x00;
    buffer[7]=0x05;
    buffer[8]=0x00;
    buffer[9]=0x00;



    serial_port_->write_some(boost::asio::buffer(buffer));

    //boost::asio::posix::stream_descriptor fd;


    serial_port_->async_read_some(boost::asio::buffer(recv_buf2_),boost::bind(handle_read, recv_buf2_, _1, _2));
    //boost::asio::async_read(*serial_port_,);


    socket_ = new boost::asio::ip::udp::socket(io_service_,boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(),18888));


    socket_->async_receive_from(boost::asio::buffer(recv_buf_),remote_endpoint_,boost::bind(udp_receiver,recv_buf_,boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));


    io_service_.run();//异步需要调用run函数驱动

    system("pause");

}