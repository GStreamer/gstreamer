<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

 <xsl:output method="text" encoding="us-ascii" omit-xml-declaration="yes" indent="no"/>
 <xsl:variable name="padding" select="string('                              ')"/>

 <xsl:template match="/element">
  <xsl:apply-templates select="name"/>
  <xsl:apply-templates select="details"/>
  <xsl:apply-templates select="object"/>
  <xsl:apply-templates select="pad-templates"/>
  <xsl:apply-templates select="element-flags"/>
  <xsl:apply-templates select="element-implementation"/>
  <xsl:apply-templates select="clocking-interaction"/>
  <xsl:apply-templates select="indexing-capabilities"/>
  <xsl:apply-templates select="pads"/>
  <xsl:apply-templates select="element-properties"/>
  <xsl:apply-templates select="dyn-params"/>
  <xsl:apply-templates select="element-signals"/>
  <xsl:apply-templates select="element-actions"/>
 </xsl:template>

 <xsl:template match="name">
  <xsl:text>Element Name: </xsl:text><xsl:value-of select="."/>
  <xsl:text>&#10;&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="details">
  <xsl:text>Factory Details:&#10;</xsl:text> 
  <xsl:text>  Long Name:&#9;</xsl:text>   <xsl:value-of select="long-name"/>   <xsl:text>&#10;</xsl:text>
  <xsl:text>  Class:&#9;</xsl:text>       <xsl:value-of select="class"/>       <xsl:text>&#10;</xsl:text>
  <xsl:text>  License:&#9;</xsl:text>     <xsl:value-of select="license"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Description:&#9;</xsl:text> <xsl:value-of select="description"/> <xsl:text>&#10;</xsl:text>
  <xsl:text>  Version:&#9;</xsl:text>     <xsl:value-of select="version"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Author(s):&#9;</xsl:text>   <xsl:value-of select="authors"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Copyright:&#9;</xsl:text>   <xsl:value-of select="copyright"/>   <xsl:text>&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template name="object">
  <xsl:param name="i"/>
  <xsl:param name="j"/>
  <xsl:if test="count($i/*) &gt; 0">
   <xsl:call-template name="object">
    <xsl:with-param name="i" select="$i/object"/>
    <xsl:with-param name="j" select="$j - 1"/>
   </xsl:call-template>
   <xsl:value-of select="substring ($padding, 1, $j * 6)"/> 
   <xsl:text> +----</xsl:text>
  </xsl:if>
  <xsl:value-of select="$i/@name"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="object">
  <xsl:call-template name="object">
   <xsl:with-param name="i" select="."/>
   <xsl:with-param name="j" select="count(.//object[(*)])"/>
  </xsl:call-template>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="pad-templates">
  <xsl:text>Pad Templates&#10;</xsl:text>
  <xsl:apply-templates select="./pad-template"/>
 </xsl:template>

 <xsl:template match="pad-template">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="direction"/> 
  <xsl:text> template: </xsl:text>
  <xsl:value-of select="name"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>    Availability: </xsl:text> <xsl:value-of select="presence"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>    Capabilities:&#10; </xsl:text> <xsl:apply-templates select="./capscomp"/>
 </xsl:template>

 <xsl:template match="capscomp">
  <xsl:apply-templates select="./caps"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="caps">
  <xsl:text>     '</xsl:text>
  <xsl:value-of select="name"/>
  <xsl:text>'&#10;</xsl:text>
  <xsl:text>        MIME type: </xsl:text>
  <xsl:value-of select="type"/>
  <xsl:text>'&#10;</xsl:text>
  <xsl:apply-templates select="./properties"/>
 </xsl:template>

 <xsl:template match="properties">
  <xsl:apply-templates select="*"/>
 </xsl:template>

 <xsl:template match="list">
  <xsl:text>        </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#9;:List&#10;</xsl:text>
  <xsl:apply-templates select="*" mode="list"/>
 </xsl:template>

 <!-- propety entries in list mode -->
 <xsl:template match="string" mode="list">
  <xsl:text>         String: '</xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>
 
 <xsl:template match="fourcc" mode="list">
  <xsl:text>         FourCC: '</xsl:text>
  <xsl:value-of select="@hexvalue"/>
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="int" mode="list">
  <xsl:text>         Integer: </xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="range" mode="list">
  <xsl:text>         Integer range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="float" mode="list">
  <xsl:text>         Float: </xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="floatrange" mode="list">
  <xsl:text>         Float range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <!-- propety entries in normal mode -->
 <xsl:template match="string">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : String: '</xsl:text>
  <xsl:value-of select="@value"/> 
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="fourcc">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : FourCC: '</xsl:text>
  <xsl:value-of select="@hexvalue"/> 
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="int">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : Integer: </xsl:text>
  <xsl:value-of select="@value"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="range"> 		
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : Integer range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="float">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : Float: </xsl:text>
  <xsl:value-of select="@value"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="floatrange"> 		
  <xsl:text>         </xsl:text>
  <xsl:value-of select="substring (concat (@name, $padding), 1, 15)"/>
  <xsl:text>     : Float range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="flag">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-flags">
  <xsl:text>Element Flags:&#10;</xsl:text>
  <xsl:apply-templates select="./flag"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="state-change">
  <xsl:text>  Has change_state() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="load">
  <xsl:text>  Has custom restore_thyself() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="save">
  <xsl:text>  Has custom save_thyself() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-implementation">
  <xsl:text>Element Implementation:&#10;</xsl:text>
  <xsl:apply-templates select="*"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="requires-clock">
  <xsl:text>   element requires a clock&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="provides-clock">
  <xsl:text>   element provides a clock: </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="clocking-interaction">
  <xsl:text>Clocking Interaction:&#10;</xsl:text>
  <xsl:choose>
   <xsl:when test="count(*) = 0">
    <xsl:text>  none&#10;</xsl:text>
   </xsl:when>
   <xsl:otherwise>
    <xsl:apply-templates select="*"/>
   </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="indexing-capabilities">
  <xsl:text>   element can do indexing</xsl:text>
 </xsl:template>

 <xsl:template match="dyn-params">
  <xsl:text>Dynamic Parameters:&#10;</xsl:text>
  <xsl:choose>
   <xsl:when test="count(*) = 0">
    <xsl:text>  none&#10;</xsl:text>
   </xsl:when>
   <xsl:otherwise>
    <xsl:apply-templates select="dyn-param"/>
   </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="pads">
  <xsl:text>Pads:&#10;</xsl:text>
  <xsl:apply-templates select="pad"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="pad">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="direction"/> 
  <xsl:text>: '</xsl:text>
  <xsl:value-of select="name"/>
  <xsl:text>'&#10;</xsl:text>
  <xsl:apply-templates select="implementation"/>
  <xsl:text>    Pad Template: '</xsl:text>
  <xsl:value-of select="template"/>
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="implementation">
  <xsl:text>    Implementation:&#10;</xsl:text>
  <xsl:apply-templates select="*"/>
 </xsl:template>

 <xsl:template match="chain-based">
  <xsl:text>      Has chainfunc(): </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="bufferpool-function">
  <xsl:text>      Has bufferpoolfunc(): </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="format">
  <xsl:text>                (</xsl:text>
  <xsl:value-of select="@id"/>
  <xsl:text>)&#9;</xsl:text>
  <xsl:value-of select="@nick"/>
  <xsl:text> (</xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>)&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="formats-function">
  <xsl:text>      Supports seeking/conversion/query formats: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates select="format"/>
 </xsl:template>

 <xsl:template match="convert-function">
  <xsl:text>      Has custom convertfunc(): </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="query-function">
  <xsl:text>      Has custom queryfunc(): </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="event-function">
  <xsl:text>      Has custom eventfunc(): </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="event">
  <xsl:text>                </xsl:text>
  <xsl:value-of select="@type"/>
  <xsl:for-each select="flag">
   <xsl:text> | </xsl:text>
   <xsl:value-of select='.'/>
  </xsl:for-each>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="event-mask-func">
  <xsl:text>        Provides event masks: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates select="event"/>
 </xsl:template>

 <xsl:template match="query-type">
  <xsl:text>                (</xsl:text>
  <xsl:value-of select="@id"/>
  <xsl:text>)&#9;</xsl:text>
  <xsl:value-of select="@nick"/>
  <xsl:text> (</xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>)&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="query-type-func">
  <xsl:text>        Provides query types: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates select="query-type"/>
 </xsl:template>

 <xsl:template match="element-properties">
  <xsl:text>Element Arguments:&#10;</xsl:text>
  <xsl:apply-templates select="element-property"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="default">
  <xsl:text>. (Default </xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>)</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="range" mode="params">
  <xsl:value-of select="substring ($padding, 1, 25)"/>
  <xsl:text>Range : </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-property|dyn-param">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="substring (concat(name, $padding), 1, 20)"/>
  <xsl:text> : </xsl:text>
  <xsl:value-of select="blurb"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:value-of select="substring ($padding, 1, 25)"/>
  <xsl:value-of select="type"/>
  <xsl:apply-templates select="default"/>
  <xsl:apply-templates select="range" mode="params"/>
 </xsl:template>

 <xsl:template match="params">
  <xsl:for-each select="type">
   <xsl:text>,&#10;</xsl:text>
   <xsl:value-of select="substring ($padding, 1, 25)"/>
   <xsl:value-of select="substring ($padding, 1, 20)"/>
   <xsl:value-of select="."/>
   <xsl:text> arg</xsl:text>
   <xsl:value-of select="position()"/>
  </xsl:for-each>
 </xsl:template>

 <xsl:template match="signal">
  <xsl:value-of select="substring (concat('&quot;', name, '&quot;', $padding), 1, 25)"/>
  <xsl:value-of select="return-type"/>
  <xsl:text> user_function </xsl:text>
  <xsl:value-of select="concat ('(', object-type, '* object')"/>
  <xsl:apply-templates select="params"/>
 </xsl:template>
 
 <xsl:template match="element-signals">
  <xsl:text>Element Signals:&#10;</xsl:text>
  <xsl:choose>
   <xsl:when test="count(*) = 0">
    <xsl:text>  none&#10;</xsl:text>
   </xsl:when>
   <xsl:otherwise>
    <xsl:for-each select="signal">
     <xsl:apply-templates select="."/>
     <xsl:text>,&#10;</xsl:text>
     <xsl:value-of select="substring ($padding, 1, 25)"/>
     <xsl:value-of select="substring ($padding, 1, 20)"/>
     <xsl:text>gpointer user_data);&#10;</xsl:text>
    </xsl:for-each>
   </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-actions">
  <xsl:text>Element Actions:&#10;</xsl:text>
  <xsl:choose>
   <xsl:when test="count(*) = 0">
    <xsl:text>  none&#10;</xsl:text>
   </xsl:when>
   <xsl:otherwise>
    <xsl:for-each select="signal">
     <xsl:apply-templates select="."/>
     <xsl:text>);&#10;</xsl:text>
    </xsl:for-each>
   </xsl:otherwise>
  </xsl:choose>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

</xsl:stylesheet>
