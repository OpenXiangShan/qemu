module DATWID_64T512 #
(
    parameter   [0:0]   WCH =    0                          ,   //写通道设定:0=读通道用;1=写通道用.

    parameter           DTW = (WCH) ? 64:512                ,   //自动计算产生,不要参数传递.主侧位宽-1值.
    parameter           STW = (WCH) ? 8: 64                 ,   //自动计算产生,不要参数传递.主侧使能-1值.
    parameter           DOW = (WCH) ? 512:64                ,   //自动计算产生,不要参数传递.从侧位宽-1值.
    parameter           SOW = (WCH) ? 64:8                      //自动计算产生,不要参数传递.从侧使能-1值.
)
(
    //1.0 复位时钟系统
    input               RST             ,                       //复位信号
    input               CLK             ,                       //AXI时钟输入
    input               HLD             ,                       //OSD缓存空不接收数据.没收到命令,不知道数据该咋处理,故不接收.
    //2.0 主侧slave AXI接主设备
    input   [DTW-1:0]   DTI             ,                       //SLV AXI接口wdata
    input   [STW-1:0]   STI             ,                       //SLV AXI接口wstrb,读通道中无用但必须接全高.
    input               LSI             ,                       //SLV AXI接口wlast
    input               HVI             ,                       //SLV AXI接口wvalid
    output              RDO             ,                       //SLV AXI接口wready
    //3.0 从侧master AXI接从设备
    output  [DOW-1:0]   DTO             ,                       //MST AXI接口wdata
    output  [SOW-1:0]   STO             ,                       //MST AXI接口wstrb
    output              LSO             ,                       //MST AXI接口wlast
    output              DVO             ,                       //MST AXI接口wvalid
    input               RDI             ,                       //MST AXI接口wready
    //4.0 cmd_fifo
    input   [  7:0]     CMD                                     //WIDCON输出控制DATWID模块信号总线
);

//1.0 sigs
    reg     [DTW-1:0]   DTIB01 =0       ;                       //
    reg     [STW-1:0]   STIB01 =0       ;                       //
    reg     [DTW-1:0]   DTIB02 =0       ;                       //
    reg     [STW-1:0]   STIB02 =0       ;                       //
    reg     [DTW-1:0]   DTIB03 =0       ;                       //
    reg     [STW-1:0]   STIB03 =0       ;                       //
    reg     [DTW-1:0]   DTIB04 =0       ;                       //
    reg     [STW-1:0]   STIB04 =0       ;                       //
    reg     [DTW-1:0]   DTIB05 =0       ;                       //
    reg     [STW-1:0]   STIB05 =0       ;                       //
    reg     [DTW-1:0]   DTIB06 =0       ;                       //
    reg     [STW-1:0]   STIB06 =0       ;                       //
    reg     [DTW-1:0]   DTIB07 =0       ;                       //
    reg     [STW-1:0]   STIB07 =0       ;                       //
    reg     [2:0]       INS    =0       ;                       //
    wire    [  1:0]     CUT             ;                       //
    wire                HED             ;                       //头部字节标识.
    wire                END             ;                       //尾部字节标识.
    wire                NRM             ;                       //'1'为标准AXI窄传输模式;'0'为满位宽整合方式.
    wire    [2:0]       SET             ;                       //'1'选择高位,'0'选择低位.由对应地址位控制.
    wire                DVI             ;                       //
//if ( WCH )
//    wire    [DOW-1:0]   NRMDTO          ;                       //
//else                            //2T1
    reg     [DOW-1:0]   NRMDTO          ;                       //
    reg     [SOW-1:0]   NRMSTO          ;                       //
    reg     [DOW-1:0]   NOMDTO          ;                       //
    reg     [SOW-1:0]   NOMSTO          ;                       //
    wire                NRMLSO          ;
    wire                NRMDVO          ;
    wire                NRMRDO          ;
    wire                NOMLSO          ;
    wire                NOMDVO          ;
    wire                NOMRDO          ;

//2.0 comp

//3.0 comb
//3.0.1公共部分
assign {CUT,HED,END,NRM,SET} = CMD                                        ;

assign DTO        = ( NRM ) ? NRMDTO : NOMDTO                             ;
assign STO        = ( NRM ) ? NRMSTO : NOMSTO                             ;
assign LSO        = ( NRM ) ? NRMLSO : NOMLSO                             ;
assign DVO        = ( NRM ) ? NRMDVO : NOMDVO                             ;
assign RDH        = ( NRM ) ? NRMRDO : NOMRDO                             ;

assign DVI        =   HVI   & ~HLD                                        ;
assign RDO        =   RDH   & ~HLD                                        ;

//3.0.1 NRM模式
generate if ( WCH )    //1T2
begin
//    assign NRMDTO = { DTI,DTI,DTI,DTI,DTI,DTI,DTI,DTI };
always @(*) begin
        NRMDTO = { DTI,DTI,DTI,DTI,DTI,DTI,DTI,DTI };
end
/*    always @(*) begin 
        case(SET)
        3'b000:
            NRMSTO <= {{(STW*7){1'b0}},STI};
        3'b001:
            NRMSTO <= {{(STW*6){1'b0}},STI,{(STW){1'b0}}};
        3'b000:
            NRMSTO <= {{(STW*5){1'b0}},STI,{(STW*2){1'b0}}};
        3'b011:
            NRMSTO <= {{(STW*4){1'b0}},STI,{(STW*3){1'b0}}};
        3'b100:
            NRMSTO <= {{(STW*3){1'b0}},STI,{(STW*4){1'b0}}};
        3'b101:
            NRMSTO <= {{(STW*2){1'b0}},STI,{(STW*5){1'b0}}};
        3'b110:
            NRMSTO <= {{(STW){1'b0}},STI,{(STW*6){1'b0}}};
        3'b111:
            NRMSTO <= {STI,{(STW*7){1'b0}}};
        endcase
    end*/
always @(*) begin
    case(SET)
        3'b000: NRMSTO = {{(STW*7){1'b0}},STI};
        3'b001: NRMSTO = {{(STW*6){1'b0}},STI,{(STW){1'b0}}};
        3'b010: NRMSTO = {{(STW*5){1'b0}},STI,{(STW*2){1'b0}}};
        3'b011: NRMSTO = {{(STW*4){1'b0}},STI,{(STW*3){1'b0}}};
        3'b100: NRMSTO = {{(STW*3){1'b0}},STI,{(STW*4){1'b0}}};
        3'b101: NRMSTO = {{(STW*2){1'b0}},STI,{(STW*5){1'b0}}};
        3'b110: NRMSTO = {{(STW){1'b0}},STI,{(STW*6){1'b0}}};
        3'b111: NRMSTO = {STI,{(STW*7){1'b0}}};
        default: NRMSTO = {{(STW*7){1'b0}},STI};
    endcase
end



    assign NRMLSO =   LSI                                                 ;
    assign NRMDVO =   DVI                                                 ;
    assign NRMRDO =   RDI                                                 ;
end
else                            //2T1
begin
//    assign NRMDTO = ( SET ) ? DTI[DTW:(DOW+1)] : DTI[DOW:00]              ;
    always @(*) begin 
        case(SET)
        3'b000:
            NRMDTO <= DTI[DOW-1:00];
        3'b001:
            NRMDTO <= DTI[DOW*2-1:DOW];
        3'b010:
            NRMDTO <= DTI[DOW*3-1:DOW*2];
        3'b011:
            NRMDTO <= DTI[DOW*4-1:DOW*3];
        3'b100:
            NRMDTO <= DTI[DOW*5-1:DOW*4];
        3'b101:
            NRMDTO <= DTI[DOW*6-1:DOW*5];
        3'b110:
            NRMDTO <= DTI[DOW*7-1:DOW*6];
        3'b111:
            NRMDTO <= DTI[DOW*8-1:DOW*7];
        endcase
    end
//    assign NRMSTO = ( SET ) ? STI[STW:(SOW+1)] : STI[SOW:00]              ;
    always @(*) begin 
        case(SET)
        3'b000:
            NRMSTO <= STI[SOW-1:00];
        3'b001:
            NRMSTO <= STI[SOW*2-1:SOW];
        3'b010:
            NRMSTO <= STI[SOW*3-1:SOW*2];
        3'b011:
            NRMSTO <= STI[SOW*4-1:SOW*3];
        3'b100:
            NRMSTO <= STI[SOW*5-1:SOW*4];
        3'b101:
            NRMSTO <= STI[SOW*6-1:SOW*5];
        3'b110:
            NRMSTO <= STI[SOW*7-1:SOW*6];
        3'b111:
            NRMSTO <= STI[SOW*8-1:SOW*7];
        endcase
    end
    assign NRMLSO =   LSI                                                 ;
    assign NRMDVO =   DVI                                                 ;
    assign NRMRDO =   RDI                                                 ;
end
endgenerate

generate if (  WCH  ) begin

//    assign NOMDTO = ( SET ) ? { DTI,DTIB01 } : {            DTI,DTI }     ;
    always @(*) begin 
        case(SET)
        3'b000:
            NOMDTO <= {DTI,DTI,DTI,DTI,DTI,DTI,DTI,DTI};
        3'b001:
            NOMDTO <= {DTI,DTI,DTI,DTI,DTI,DTI,DTI,DTIB01};
        3'b010:
            NOMDTO <= {DTI,DTI,DTI,DTI,DTI,DTI,DTIB02,DTIB01};
        3'b011:
            NOMDTO <= {DTI,DTI,DTI,DTI,DTI,DTIB03,DTIB02,DTIB01};
        3'b100:
            NOMDTO <= {DTI,DTI,DTI,DTI,DTIB04,DTIB03,DTIB02,DTIB01};
        3'b101:
            NOMDTO <= {DTI,DTI,DTI,DTIB05,DTIB04,DTIB03,DTIB02,DTIB01};
        3'b110:
            NOMDTO <= {DTI,DTI,DTIB06,DTIB05,DTIB04,DTIB03,DTIB02,DTIB01};
        3'b111:
            NOMDTO <= {DTI,DTIB07,DTIB06,DTIB05,DTIB04,DTIB03,DTIB02,DTIB01};
        endcase
    end
//    assign NOMSTO = ( SET ) ? { STI,STIB01 } : {{(DTW+1){1'b0}},STI }     ;
    always @(*) begin 
        case(SET)
        3'b000:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},STI};
        3'b001:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},STI,STIB01};
        3'b010:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},STI,STIB02,STIB01};
        3'b011:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},STI,STIB03,STIB02,STIB01};
        3'b100:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},{(DTW){1'b0}},STI,STIB04,STIB03,STIB02,STIB01};
        3'b101:
            NOMSTO <= {{(DTW){1'b0}},{(DTW){1'b0}},STI,STIB05,STIB04,STIB03,STIB02,STIB01};
        3'b110:
            NOMSTO <= {{(DTW){1'b0}},STI,STIB06,STIB05,STIB04,STIB03,STIB02,STIB01};
        3'b111:
            NOMSTO <= {STI,STIB07,STIB06,STIB05,STIB04,STIB03,STIB02,STIB01};
        endcase
    end
    assign NOMLSO =   LSI                                                 ;
    assign NOMDVO =   DVI   & ( SET==3'b111 | LSI  )                      ;
    assign NOMRDO =   RDI                                                 ;

end else  begin

//    assign NOMDTO = ( INS ) ? DTI[DTW:(DOW+1)] : DTI[DOW:00]              ;         //
    always @(*) begin 
        case(INS)
        3'b000:
            NOMDTO <= DTI[DOW-1:00];
        3'b001:
            NOMDTO <= DTI[DOW*2-1:DOW];
        3'b010:
            NOMDTO <= DTI[DOW*3-1:DOW*2];
        3'b011:
            NOMDTO <= DTI[DOW*4-1:DOW*3];
        3'b100:
            NOMDTO <= DTI[DOW*5-1:DOW*4];
        3'b101:
            NOMDTO <= DTI[DOW*6-1:DOW*5];
        3'b110:
            NOMDTO <= DTI[DOW*7-1:DOW*6];
        3'b111:
            NOMDTO <= DTI[DOW*8-1:DOW*7];
        endcase
    end
//    assign NOMSTO = ( INS ) ? STI[STW:(SOW+1)] : STI[SOW:00]              ;         //
    always @(*) begin 
        case(INS)
        3'b000:
            NOMSTO <= STI[SOW-1:00];
        3'b001:
            NOMSTO <= STI[SOW*2-1:SOW];
        3'b010:
            NOMSTO <= STI[SOW*3-1:SOW*2];
        3'b011:
            NOMSTO <= STI[SOW*4-1:SOW*3];
        3'b100:
            NOMSTO <= STI[SOW*5-1:SOW*4];
        3'b101:
            NOMSTO <= STI[SOW*6-1:SOW*5];
        3'b110:
            NOMSTO <= STI[SOW*7-1:SOW*6];
        3'b111:
            NOMSTO <= STI[SOW*8-1:SOW*7];
        endcase
    end
    assign NOMLSO =   LSI   &  (     (INS==3'b111) | (END & CUT[0]         ))       ;         //R2T1去掉头尾多余的无效数据.头尾是否多余可计算得到
    assign NOMDVO =   DVI   & ~(HED&~(INS==3'b111)&CUT[1]) & ~(END& (INS==3'b111)&CUT[0])     ;         //R2T1去掉头尾多余的无效数据.头尾是否多余可计算得到
    assign NOMRDO =   RDI   &        (INS==3'b111)                                  ;         //??

end endgenerate

//4.0 regs
generate if (  WCH  ) begin
    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB01 <= 'b0;
            STIB01 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB01 <= 'b0;
            STIB01 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b000)
        begin
            DTIB01 <= DTI;
            STIB01 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB02 <= 'b0;
            STIB02 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB02 <= 'b0;
            STIB02 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b001)
        begin
            DTIB02 <= DTI;
            STIB02 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB03 <= 'b0;
            STIB03 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB03 <= 'b0;
            STIB03 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b010)
        begin
            DTIB03 <= DTI;
            STIB03 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB04 <= 'b0;
            STIB04 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB04 <= 'b0;
            STIB04 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b011)
        begin
            DTIB04 <= DTI;
            STIB04 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB05 <= 'b0;
            STIB05 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB05 <= 'b0;
            STIB05 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b100)
        begin
            DTIB05 <= DTI;
            STIB05 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB06 <= 'b0;
            STIB06 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB06 <= 'b0;
            STIB06 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b101)
        begin
            DTIB06 <= DTI;
            STIB06 <= STI;
        end
    end

    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB07 <= 'b0;
            STIB07 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB07 <= 'b0;
            STIB07 <= 'b0;
        end
        else if ( DVI & RDO & SET==3'b110)
        begin
            DTIB07 <= DTI;
            STIB07 <= STI;
        end
    end

end else if (  ~WCH ) begin
    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
            INS <= 3'b0;
        else if ( DVI & RDO )
            INS <= 3'b0;
        else if ( DVI & RDI )
            INS <= INS + 3'b1;
    end
end endgenerate

//5.0 sims

endmodule


